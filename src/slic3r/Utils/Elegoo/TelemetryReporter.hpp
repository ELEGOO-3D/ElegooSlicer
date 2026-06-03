#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "slic3r/Utils/Singleton.hpp"

namespace Slic3r {

class TelemetryReporter : public Singleton<TelemetryReporter>
{
    friend class Singleton<TelemetryReporter>;

    struct DeviceInfoSnapshot {
        std::string os;
        std::string osName;
        std::string osVersion;
        std::string osArch;
        std::string screenResolution;
        std::string appLanguage;
        std::string systemLanguage;
        std::string cpuModel;
        uint32_t    cpuLogicalCores{0};
        std::string gpuModel;
        std::string gpuDriverVersion;
        uint64_t    gpuVramMb{0};
        double      memoryTotalGb{0.0};
        std::string openglVersion;
        std::string openglVendor;
        std::string openglRenderer;
    };

public:
    TelemetryReporter(const TelemetryReporter&) = delete;
    TelemetryReporter& operator=(const TelemetryReporter&) = delete;

    void init();
    void stop();
    void updateGraphicsContext();
    void reportEvent(const std::string& eventName,
                     const nlohmann::json& content = nlohmann::json(),
                     const std::string& pageName = "");

private:
    struct PendingEvent {
        int64_t        eventTime{0};
        std::string    uid;
        std::string    eventName;
        nlohmann::json content;
        std::string    pageName;
    };

    TelemetryReporter();
    ~TelemetryReporter();

    void initializeStaticContextLocked();
    void updateGraphicsContextLocked();
    void startWorkerLocked();
    /**
        * Reports persisted runtime entries whose owning process is no longer alive.
     */
    void reportPendingAppRuntimeOnStartup();
    /**
     * Reports app runtime events at the boundary of each day.
     */
    void reportAppRuntimeOnDayBoundary();
    /**
        * Persists the current process runtime snapshot on a fixed cadence so
        * unexpected exits only lose a small tail of session time.
     */
    void persistRuntimeStatePeriodically();
    void runWorker();
    bool sendBatch(const std::vector<std::string>& batch);
    PendingEvent buildPendingEvent(const std::string& eventName,
                                   const nlohmann::json& content,
                                   const std::string& pageName) const;
    nlohmann::json serializeEvent(const PendingEvent& event) const;
    /**
     * Builds the payload for app runtime events.
     */
    nlohmann::json buildAppRuntimePayload(int64_t runtimeSec,
                                         const std::string& lastSessionId,
                                         int64_t lastSessionRuntimeSec) const;
    /**
     * Returns the current runtime in seconds.
     */
    int64_t getCurrentRuntimeSecLocked(std::chrono::system_clock::time_point now) const;

private:
    mutable std::mutex mStateMutex;
    std::atomic<bool> mInitialized{false};
    std::atomic<bool> mAccepting{false};
    std::atomic<bool> mWorkerRunning{false};

    std::mutex              mQueueMutex;
    std::condition_variable mQueueCv;
    std::deque<std::string> mQueue;  // Pre-serialized JSON strings
    std::thread             mWorkerThread;
    long                    mRetrySkipCount{0};

    // Offline cache
    bool isUserLoggedIn() const;
    void loadCachedEvents();
    void recoverDeadProcessCachedEventsOnStartup();
    void backupCacheToFile();
    std::string getCacheDirectory() const;
    std::string getCacheFilePath() const;
    std::atomic<bool> mCacheDirty{false};  // Track if cache has changed since last backup

    std::string mSessionId;
    std::string mDeviceId;
    std::string mPlatform;
    /**
     * The time point when the current session started.
     */
    std::chrono::system_clock::time_point mSessionStartedAt{};
    /**
     * The next scheduled time for reporting runtime events.
     */
    std::chrono::system_clock::time_point mNextRuntimeReportAt{};
    /**
     * The next scheduled time for persisting the current runtime snapshot.
     */
    std::chrono::system_clock::time_point mNextRuntimeStatePersistAt{};
    unsigned mProcessId{0};
    DeviceInfoSnapshot mDeviceInfo;
    std::string mTelemetryUrl;
    std::string mUserAgent;
};

} // namespace Slic3r
