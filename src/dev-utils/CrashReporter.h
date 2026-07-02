#pragma once

#include <string>

/**
 * @brief Crash reporter using Sentry Native SDK (Crashpad backend)
 *
 * Captures crashes on Windows/macOS/Linux. Local crash data is stored in
 * dataDir/sentry/. Uploading is controlled separately by setEnabled().
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
     * @brief Enable or disable crash report uploading
     *
     * The crash handler stays installed either way so local crash data can
     * still be generated. Events are not uploaded while disabled.
     * @param enabled true to allow sending, false to suppress
     */
    static void setEnabled(bool enabled);

    /**
     * @brief Trigger a test crash for debugging crash reporting
     */
    static void triggerTestCrash();
};
