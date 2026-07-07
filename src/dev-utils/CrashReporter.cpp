#include "CrashReporter.h"

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
        if (!boost::filesystem::exists(handlerPath)) {
            BOOST_LOG_TRIVIAL(error) << "crashpad_handler not found: " << handlerPath.string();
            return false;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to locate crashpad_handler: " << e.what();
        return false;
    }

    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, dsn);
    sentry_options_set_database_path(options, sentryDir.string().c_str());
    sentry_options_set_release(options, "elegoo-slicer@" ELEGOOSLICER_VERSION);
    sentry_options_set_handler_path(options, handlerPath.string().c_str());
    sentry_options_set_cache_keep(options, SENTRY_CACHE_KEEP_OFFLINE);
    sentry_options_set_cache_max_age(options, 30 * 24 * 60 * 60);
    sentry_options_set_cache_max_size(options, 128 * 1024 * 1024);
    sentry_options_set_cache_max_items(options, 10);
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
    if (!sInitialized) {
        BOOST_LOG_TRIVIAL(warning) << "Crash reporter not initialized (login required)";
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "Triggering test crash for Sentry crash reporting verification";
    volatile int* ptr = nullptr;
    *ptr = 42;
}
