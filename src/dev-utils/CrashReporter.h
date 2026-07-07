#pragma once

#include <string>

/**
 * @brief Crash reporter using Sentry Native SDK (Crashpad backend)
 *
 * Initialized after Elegoo user login succeeds. Shutdown on application exit only.
 */
class CrashReporter
{
public:
    /**
     * @brief Initialize Sentry crash reporter (call only when user is logged in)
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
