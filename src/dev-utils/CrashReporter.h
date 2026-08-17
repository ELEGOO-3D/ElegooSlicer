#pragma once

#include <string>

/**
 * @brief Crash reporter using Sentry Native SDK (Crashpad backend)
 *
 * Started at process startup so macOS Crashpad fork() is safe.
 */
class CrashReporter
{
public:
    static bool init(const std::string& dataDir);
    static void close();
    static void triggerTestCrash();
};
