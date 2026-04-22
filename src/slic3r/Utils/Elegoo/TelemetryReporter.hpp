#pragma once

#include <atomic>
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

public:
    TelemetryReporter(const TelemetryReporter&) = delete;
    TelemetryReporter& operator=(const TelemetryReporter&) = delete;

    void init();
    void stop();
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
    void startWorkerLocked();
    void runWorker();
    bool sendBatch(const std::vector<PendingEvent>& batch);
    PendingEvent buildPendingEvent(const std::string& eventName,
                                   const nlohmann::json& content,
                                   const std::string& pageName) const;
    nlohmann::json serializeEvent(const PendingEvent& event) const;

private:
    std::mutex       mStateMutex;
    std::atomic<bool> mInitialized{false};
    std::atomic<bool> mAccepting{false};
    std::atomic<bool> mWorkerRunning{false};

    std::mutex              mQueueMutex;
    std::condition_variable mQueueCv;
    std::deque<PendingEvent> mQueue;
    std::thread             mWorkerThread;

    std::string mSessionId;
    std::string mDeviceId;
    std::string mPlatform;
    std::string mOsName;
    std::string mAppLanguage;
    std::string mSystemLanguage;
    std::string mScreenResolution;
    std::string mTelemetryUrl;
    std::string mUserAgent;
};

} // namespace Slic3r
