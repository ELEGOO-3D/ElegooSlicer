#include "CrashReporter.h"

#include <algorithm>
#include <ctime>
#include <vector>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

#include <sentry.h>
#include "libslic3r_version.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static bool sInitialized = false;

static void pruneOldRuns(const boost::filesystem::path& sentryDir, int keepCount)
{
    try {
        std::vector<std::pair<std::time_t, boost::filesystem::path>> runs;
        for (auto& entry : boost::filesystem::directory_iterator(sentryDir)) {
            if (boost::filesystem::is_directory(entry)) {
                runs.emplace_back(boost::filesystem::last_write_time(entry), entry.path());
            }
        }
        if (runs.size() <= static_cast<size_t>(keepCount))
            return;

        std::sort(runs.begin(), runs.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (size_t i = keepCount; i < runs.size(); ++i) {
            boost::filesystem::remove_all(runs[i].second);
            BOOST_LOG_TRIVIAL(debug) << "Pruned old sentry run: " << runs[i].second.string();
        }
        BOOST_LOG_TRIVIAL(info) << "Pruned " << (runs.size() - keepCount) << " old sentry run(s), keeping latest " << keepCount;
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "Failed to prune old sentry runs";
    }
}

bool CrashReporter::init(const std::string& dataDir)
{
    if (sInitialized) {
        BOOST_LOG_TRIVIAL(warning) << "Crash reporter already initialized";
        return true;
    }

    const char* dsn = nullptr;
#ifdef _WIN32
    dsn = SENTRY_DSN_WIN;
#elif defined(__APPLE__)
    dsn = SENTRY_DSN_MAC;
#elif defined(__linux__)
    dsn = SENTRY_DSN_LINUX;
#endif
    if (!dsn || dsn[0] == '\0') {
        dsn = getenv("SENTRY_DSN");
    }
    if (!dsn || dsn[0] == '\0') {
        BOOST_LOG_TRIVIAL(warning) << "Sentry DSN not configured, crash reporting disabled. "
                                   << "Set SENTRY_DSN_WIN/MAC/LINUX in .env or SENTRY_DSN environment variable.";
        return false;
    }

    boost::filesystem::path sentryDir = boost::filesystem::path(dataDir) / "sentry";
    try {
        if (!boost::filesystem::exists(sentryDir)) {
            boost::filesystem::create_directories(sentryDir);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to create sentry directory: " << e.what();
        return false;
    }

    boost::filesystem::path handlerPath;
    try {
#ifdef _WIN32
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        boost::filesystem::path exeDir = boost::filesystem::path(exePath).parent_path();
        handlerPath = exeDir / "crashpad" / "crashpad_handler.exe";
        if (!boost::filesystem::exists(handlerPath)) {
            handlerPath = exeDir / "crashpad_handler.exe";
        }
#else
        boost::filesystem::path exeDir = boost::dll::program_location().parent_path();
        handlerPath = exeDir / "crashpad" / "crashpad_handler";
        if (!boost::filesystem::exists(handlerPath)) {
            handlerPath = exeDir / "crashpad_handler";
        }
#endif
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to locate crashpad_handler: " << e.what();
        return false;
    }

    pruneOldRuns(sentryDir, 3);

    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, dsn);
    sentry_options_set_database_path(options, sentryDir.string().c_str());
    sentry_options_set_release(options, "elegoo-slicer@" ELEGOOSLICER_VERSION);
    sentry_options_set_handler_path(options, handlerPath.string().c_str());
    sentry_options_set_shutdown_timeout(options, 5000);
#ifndef NDEBUG
    sentry_options_set_debug(options, 1);
#endif

    int result = sentry_init(options);
    if (result == 0) {
        BOOST_LOG_TRIVIAL(info) << "Sentry crash reporter initialized successfully";
        BOOST_LOG_TRIVIAL(info) << "  Database: " << sentryDir.string();
        BOOST_LOG_TRIVIAL(info) << "  Handler: " << handlerPath.string();
        BOOST_LOG_TRIVIAL(info) << "  Version: " << ELEGOOSLICER_VERSION;
        sInitialized = true;
        return true;
    } else {
        BOOST_LOG_TRIVIAL(error) << "Failed to initialize Sentry (error code: " << result << ")";
        return false;
    }
}

void CrashReporter::close()
{
    if (sInitialized) {
        BOOST_LOG_TRIVIAL(info) << "Shutting down Sentry crash reporter";
        sentry_close();
        sInitialized = false;
    }
}

void CrashReporter::triggerTestCrash()
{
    BOOST_LOG_TRIVIAL(info) << "Triggering test crash for Sentry crash reporting verification";
    volatile int* ptr = nullptr;
    *ptr = 42;
}
