#include "CrashReporter.h"
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <map>
#include <vector>

// Crashpad headers
#include <client/crashpad_client.h>
#include <client/crash_report_database.h>
#include <client/settings.h>

using namespace crashpad;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#endif
#include <windows.h>
#include <intrin.h>
#endif

#ifndef ELEGOOSLICER_VERSION
#define ELEGOOSLICER_VERSION "Unknown"
#endif

static bool sInitialized = false;

void CrashReporter::cleanupOldDmpFiles(const std::string& dataDir)
{
    try {
        boost::filesystem::path logDir = boost::filesystem::path(dataDir) / "log";
        if (!boost::filesystem::exists(logDir))
            return;
        
        size_t deleted_count = 0;
        for (auto& entry : boost::filesystem::directory_iterator(logDir)) {
            if (boost::filesystem::is_regular_file(entry)) {
                std::string filename = entry.path().filename().string();
                if (filename.size() >= 4 && 
                    (filename.substr(filename.size() - 4) == ".dmp" || 
                     filename.substr(filename.size() - 4) == ".DMP")) {
                    try {
                        boost::filesystem::remove(entry.path());
                        deleted_count++;
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(warning) << "Failed to delete " << entry.path() << ": " << e.what();
                    }
                }
            }
        }
        
        if (deleted_count > 0) {
            BOOST_LOG_TRIVIAL(info) << "Cleaned up " << deleted_count << " old .dmp file(s) from log directory";
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Error during old .dmp files cleanup: " << e.what();
    }
}

size_t CrashReporter::cleanupOldCrashReports(void* database, int keepCount)
{
    if (!database || keepCount < 0) {
        return 0;
    }
    
    CrashReportDatabase* db = static_cast<CrashReportDatabase*>(database);
    
    std::vector<CrashReportDatabase::Report> all_reports;
    
    std::vector<CrashReportDatabase::Report> pending_reports;
    db->GetPendingReports(&pending_reports);
    all_reports.insert(all_reports.end(), pending_reports.begin(), pending_reports.end());
    
    std::vector<CrashReportDatabase::Report> completed_reports;
    db->GetCompletedReports(&completed_reports);
    all_reports.insert(all_reports.end(), completed_reports.begin(), completed_reports.end());
    
    if (all_reports.empty()) {
        BOOST_LOG_TRIVIAL(info) << "No crash reports found (database is empty)";
        return 0;
    }
    
    std::sort(all_reports.begin(), all_reports.end(),
        [](const CrashReportDatabase::Report& a, const CrashReportDatabase::Report& b) {
            return a.creation_time > b.creation_time;
        });
    
    size_t deleted_count = 0;
    
    if (all_reports.size() > static_cast<size_t>(keepCount)) {
        for (size_t i = keepCount; i < all_reports.size(); ++i) {
            CrashReportDatabase::OperationStatus status = db->DeleteReport(all_reports[i].uuid);
            if (status == CrashReportDatabase::kNoError) {
                deleted_count++;
                BOOST_LOG_TRIVIAL(debug) << "Deleted old crash report: " << all_reports[i].uuid.ToString();
            } else {
                BOOST_LOG_TRIVIAL(warning) << "Failed to delete crash report: " << all_reports[i].uuid.ToString();
            }
        }
        if (deleted_count > 0) {
            BOOST_LOG_TRIVIAL(info) << "Cleaned up " << deleted_count << " old crash report(s), keeping latest " << keepCount;
        }
    }
    
    // Use parentheses to prevent macro expansion of min
    size_t remaining_count = (std::min)(all_reports.size(), static_cast<size_t>(keepCount));
    BOOST_LOG_TRIVIAL(info) << "Found " << remaining_count << " crash report(s) (keeping latest " << keepCount << ")";
    for (size_t i = 0; i < remaining_count; ++i) {
        const auto& report = all_reports[i];
        base::FilePath report_path = report.file_path;
        BOOST_LOG_TRIVIAL(info) << "  Report: " << report_path.BaseName().value() 
                                << " (UUID: " << report.uuid.ToString() << ")";
    }
    
    return deleted_count;
}

bool CrashReporter::init(const std::string& dataDir)
{
    if (sInitialized) {
        BOOST_LOG_TRIVIAL(warning) << "Crash reporter already initialized";
        return true;
    }
    
    boost::filesystem::path crashDir = boost::filesystem::path(dataDir) / "crashes";
    try {
        if (!boost::filesystem::exists(crashDir)) {
            boost::filesystem::create_directories(crashDir);
        }
        
        cleanupOldDmpFiles(dataDir);
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to create crash directory: " << e.what();
        return false;
    }
    
    try {
#ifdef _WIN32
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        boost::filesystem::path exeDir = boost::filesystem::path(exePath).parent_path();
        boost::filesystem::path handlerPath = exeDir / "crashpad" / "crashpad_handler.exe";
        
        if (!boost::filesystem::exists(handlerPath)) {
            handlerPath = exeDir / "crashpad_handler.exe";
        }
        
        if (!boost::filesystem::exists(handlerPath)) {
            BOOST_LOG_TRIVIAL(warning) << "Crashpad handler not found: " << handlerPath.string();
            return false;
        }
        
        base::FilePath dbPath(crashDir.wstring());
#else
        boost::filesystem::path exePath = boost::dll::program_location();
        boost::filesystem::path exeDir = exePath.parent_path();
        boost::filesystem::path handlerPath = exeDir / "crashpad" / "crashpad_handler";
        
        if (!boost::filesystem::exists(handlerPath)) {
            handlerPath = exeDir / "crashpad_handler";
        }
        
        if (!boost::filesystem::exists(handlerPath)) {
            BOOST_LOG_TRIVIAL(warning) << "Crashpad handler not found: " << handlerPath.string();
            return false;
        }
        
        base::FilePath dbPath(crashDir.string());
#endif
        
        std::unique_ptr<CrashReportDatabase> database = CrashReportDatabase::Initialize(dbPath);
        if (!database) {
            BOOST_LOG_TRIVIAL(error) << "Failed to initialize Crashpad database";
            return false;
        }
        
        Settings* settings = database->GetSettings();
        if (settings) {
            settings->SetUploadsEnabled(false);
        }
        
        const int KEEP_REPORTS_COUNT = 3;
        cleanupOldCrashReports(database.get(), KEEP_REPORTS_COUNT);
        
        static CrashpadClient client;
#ifdef _WIN32
        base::FilePath handler(handlerPath.wstring());
#else
        base::FilePath handler(handlerPath.string());
#endif
        
        std::map<std::string, std::string> annotations;
        annotations["product"] = "ElegooSlicer";
        annotations["version"] = ELEGOOSLICER_VERSION;
        
        bool success = client.StartHandler(
            handler,
            dbPath,
            base::FilePath(),
            "",
            annotations,
            std::vector<std::string>(),
            true,
            true
        );
        
        if (success) {
            BOOST_LOG_TRIVIAL(info) << "Crashpad initialized successfully";
            BOOST_LOG_TRIVIAL(info) << "  Crash dumps: " << crashDir.string();
            sInitialized = true;
        } else {
            BOOST_LOG_TRIVIAL(error) << "Failed to start Crashpad";
        }
        
        return success;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Crashpad initialization error: " << e.what();
        return false;
    }
}

#ifdef _WIN32
void CrashReporter::triggerTestCrash()
{
    BOOST_LOG_TRIVIAL(info) << "Triggering test crash for crash reporting verification";
    volatile int* ptr = nullptr;
    *ptr = 42;
}
#else
void CrashReporter::triggerTestCrash()
{
    BOOST_LOG_TRIVIAL(info) << "Triggering test crash for crash reporting verification";
    abort();
}
#endif
