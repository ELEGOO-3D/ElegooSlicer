#pragma once

#include <string>

/**
 * @brief Crash reporter using Google Crashpad
 * 
 * Handles unhandled exceptions and generates minidump files. Crash reports are stored
 * in dataDir/crashes/ and automatically pruned to keep only the latest 3 reports.
 */
class CrashReporter
{
public:
    /**
     * @brief Initialize Crashpad crash reporter
     * @param dataDir Application data directory
     * @return true if initialized successfully
     */
    static bool init(const std::string& dataDir);

    /**
     * @brief Trigger a test crash for debugging crash reporting
     */
    static void triggerTestCrash();

private:
    /**
     * @brief Clean up old .dmp files from log directory (from previous non-Crashpad versions)
     * @param dataDir Application data directory
     */
    static void cleanupOldDmpFiles(const std::string& dataDir);
    
    /**
     * @brief Clean up old crash reports from Crashpad database, keep only latest N reports
     * @param database Crashpad database instance (void* to avoid including crashpad headers)
     * @param keepCount Number of recent reports to keep
     * @return Number of reports deleted
     */
    static size_t cleanupOldCrashReports(void* database, int keepCount);
};
