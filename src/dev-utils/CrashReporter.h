#pragma once

#include <string>

/**
 * @brief Crash reporter using Sentry Native SDK (Crashpad backend)
 *
 * Captures crashes on Windows/macOS/Linux and uploads reports to Sentry.
 * Crash reports are stored in dataDir/sentry/ before upload.
 */
class CrashReporter
{
public:
    /**
     * @brief Initialize Sentry crash reporter
     * @param dataDir Application data directory
     * @return true if initialized successfully
     */
    static bool init(const std::string& dataDir);

    /**
     * @brief Shutdown Sentry, flushing pending events before exit
     */
    static void close();

    /**
     * @brief Trigger a test crash for debugging crash reporting
     */
    static void triggerTestCrash();
};
