#include "TelemetryReporter.hpp"

#include "slic3r/Utils/Elegoo/ElegooUtils.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <wx/platinfo.h>
#include <wx/display.h>
#include <wx/utils.h>

#include "IPCClient.hpp"
#include "MultiInstanceCoordinator.hpp"
#include "PrinterNetwork.hpp"
#include "UserNetworkManager.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

#ifdef _WIN32
#elif defined(__APPLE__)
#include <sys/utsname.h>
#else
#include <sys/utsname.h>
#endif

namespace Slic3r {
namespace {

constexpr const char* kSdkVersion               = "1.0.0";
constexpr const char* kAppName                  = "elegoo-slicer";
constexpr const char* kAppId                    = "7";
constexpr size_t      kBatchSize                = 20;
constexpr size_t      kMaxQueueSize             = 1000;
constexpr long        kFlushIntervalMs          = 1000;
constexpr long        kHttpConnectTimeoutSecond = 5;
constexpr long        kHttpTimeoutSecond        = 10;

std::string normalizePlatform()
{
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "mac";
#else
    return "linux";
#endif
}

std::string buildOsName()
{
#ifdef _WIN32
    int major = 0;
    int minor = 0;
    int build = 0;
    wxGetOsVersion(&major, &minor, &build);

    if (major == 10) {
        return build >= 22000 ? "Windows 11" : "Windows 10";
    }
    if (major == 6 && minor == 3) {
        return "Windows 8.1";
    }
    if (major == 6 && minor == 2) {
        return "Windows 8";
    }
    if (major == 6 && minor == 1) {
        return "Windows 7";
    }
    if (major == 6 && minor == 0) {
        return "Windows Vista";
    }
    if (major == 5 && minor == 1) {
        return "Windows XP";
    }
    if (major > 0) {
        return "Windows " + std::to_string(major) + "." + std::to_string(minor);
    }
    return "Windows";
#elif defined(__APPLE__)
    wxPlatformInfo platformInfo;
    const int major = platformInfo.GetOSMajorVersion();
    const int minor = platformInfo.GetOSMinorVersion();

    if (major > 0) {
        return "macOS " + std::to_string(major) + "." + std::to_string(minor);
    }
    return "macOS";
#else
    struct utsname systemInfo;
    if (uname(&systemInfo) == 0) {
        std::string osName = systemInfo.sysname;
        if (systemInfo.release[0] != '\0') {
            osName += " ";
            osName += systemInfo.release;
        }
        return osName;
    }
    return "Linux";
#endif
}

std::string generateSessionId()
{
    std::string sessionId = boost::uuids::to_string(boost::uuids::random_generator()());
    boost::erase_all(sessionId, "-");
    return sessionId;
}

std::string getScreenResolution()
{
    if (wxDisplay::GetCount() <= 0) {
        return std::string();
    }

    wxDisplay display(0u);
    const wxRect area = display.GetClientArea();
    if (area.width <= 0 || area.height <= 0) {
        return std::string();
    }

    return std::to_string(area.width) + "x" + std::to_string(area.height);
}

} // namespace

TelemetryReporter::TelemetryReporter() = default;

TelemetryReporter::~TelemetryReporter()
{
    stop();
}

void TelemetryReporter::init()
{
    std::lock_guard<std::mutex> lock(mStateMutex);
    if (!mInitialized.load(std::memory_order_acquire)) {
        initializeStaticContextLocked();
        mInitialized.store(true, std::memory_order_release);
    }

    if (MultiInstanceCoordinator::getInstance()->isMaster()) {
        startWorkerLocked();
    }
}

void TelemetryReporter::stop()
{
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        mAccepting.store(false, std::memory_order_release);
    }
    mQueueCv.notify_all();

    if (mWorkerThread.joinable()) {
        mWorkerThread.join();
    }

    mWorkerRunning.store(false, std::memory_order_release);
    mInitialized.store(false, std::memory_order_release);
}

void TelemetryReporter::reportEvent(const std::string& eventName,
                                    const nlohmann::json& content,
                                    const std::string& pageName)
{
    if (eventName.empty()) {
        return;
    }

    if (!mInitialized.load(std::memory_order_acquire)) {
        init();
    }

    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        IPCClient::getInstance()->reportTelemetryEvent(eventName, content, pageName);
        return;
    }

    if (!mAccepting.load(std::memory_order_acquire)) {
        return;
    }

    PendingEvent event = buildPendingEvent(eventName, content, pageName);
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        if (mQueue.size() >= kMaxQueueSize) {
            mQueue.pop_front();
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: queue full, dropping oldest event";
        }
        mQueue.push_back(std::move(event));
    }
    mQueueCv.notify_one();
}

void TelemetryReporter::initializeStaticContextLocked()
{
    mSessionId      = generateSessionId();
    mDeviceId       = ElegooUtils::getDeviceId();
    mPlatform       = normalizePlatform();
    mOsName         = buildOsName();
    mScreenResolution = getScreenResolution();
    mAppLanguage    = GUI::wxGetApp().app_config ? GUI::wxGetApp().app_config->get_language_code() : std::string("en");
    mSystemLanguage = GUI::wxGetApp().app_config ? GUI::wxGetApp().app_config->getSystemLanguage() : std::string("en");

    auto networkHelper = NetworkFactory::createNetworkHelper(htElegooLink);
    if (networkHelper) {
        mTelemetryUrl = networkHelper->getTelemetryUrl();
        mUserAgent  = networkHelper->getUserAgent();
    }
}

void TelemetryReporter::startWorkerLocked()
{
    if (mWorkerRunning.load(std::memory_order_acquire)) {
        return;
    }

    mAccepting.store(true, std::memory_order_release);
    mWorkerRunning.store(true, std::memory_order_release);
    mWorkerThread = std::thread([this]() { runWorker(); });
}

void TelemetryReporter::runWorker()
{
    while (true) {
        std::vector<PendingEvent> batch;
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mQueueCv.wait_for(lock, std::chrono::milliseconds(kFlushIntervalMs), [this]() {
                return !mAccepting.load(std::memory_order_acquire) || mQueue.size() >= kBatchSize;
            });

            if (!mAccepting.load(std::memory_order_acquire) && mQueue.empty()) {
                break;
            }

            if (mQueue.empty()) {
                continue;
            }

            const size_t batchCount = std::min(kBatchSize, mQueue.size());
            batch.reserve(batchCount);
            for (size_t index = 0; index < batchCount; ++index) {
                batch.push_back(std::move(mQueue.front()));
                mQueue.pop_front();
            }
        }

        if (!sendBatch(batch)) {
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to upload telemetry batch, dropped events=" << batch.size();
        }
    }
}

bool TelemetryReporter::sendBatch(const std::vector<PendingEvent>& batch)
{
    if (batch.empty() || mTelemetryUrl.empty()) {
        return false;
    }
    nlohmann::json payload = nlohmann::json::array();
    for (const PendingEvent& event : batch) {
        payload.push_back(serializeEvent(event));
    }

    std::string payloadStr = payload.dump();
    std::string path="/api/v1/event-report-server/events";
    if (mTelemetryUrl.back() == '/') {
        path = "api/v1/event-report-server/events'";
    }
    bool success = true;
    Http http = Http::post(mTelemetryUrl + path);
    http.header("Content-Type", "application/json")
        .timeout_connect(kHttpConnectTimeoutSecond)
        .timeout_max(kHttpTimeoutSecond)
        .set_post_body(payload.dump())
        .on_complete([&success](std::string body, unsigned httpStatus) {
            // BOOST_LOG_TRIVIAL(info) << "TelemetryReporter: upload complete, status=" << httpStatus << ", body=" << body;
            success = httpStatus >= 200 && httpStatus < 300;
        })
        .on_error([&success](std::string body, std::string error, unsigned httpStatus) {
            success = false;
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: upload error, status=" << httpStatus
                                       << ", error=" << error << ", body=" << body;
        });

    if (!mUserAgent.empty()) {
        http.header("User-Agent", mUserAgent);
    }

    http.perform_sync();
    return success;
}

TelemetryReporter::PendingEvent TelemetryReporter::buildPendingEvent(const std::string& eventName,
                                                                     const nlohmann::json& content,
                                                                     const std::string& pageName) const
{
    PendingEvent event;
    event.eventTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    event.eventName = eventName;
    event.content   = content;
    event.pageName  = pageName;
    event.uid       = UserNetworkManager::getInstance()->getUserInfo().userId;
    return event;
}

nlohmann::json TelemetryReporter::serializeEvent(const PendingEvent& event) const
{
    nlohmann::json json = nlohmann::json::object();
    json["event_time"] = event.eventTime;
    if (!event.uid.empty()) {
        json["uid"] = event.uid;
    }
    json["app_name"]    = kAppName;
    json["app_id"]      = kAppId;
    json["device_id"]   = mDeviceId;
    json["session_id"]  = mSessionId;
    json["platform"]    = mPlatform;
    json["sdk_version"] = kSdkVersion;
    json["app_version"] = SLIC3R_VERSION;

    nlohmann::json deviceInfo = nlohmann::json::object();
    deviceInfo["os"] = mOsName;
    if (!mScreenResolution.empty()) {
        deviceInfo["screen_resolution"] = mScreenResolution;
    }
    if (!mAppLanguage.empty()) {
        deviceInfo["app_lang"] = mAppLanguage;
    }
    if (!mSystemLanguage.empty()) {
        deviceInfo["sys_lang"] = mSystemLanguage;
    }
    json["device_info"] = std::move(deviceInfo);
    json["event_name"]  = event.eventName;
    if (!event.pageName.empty()) {
        json["page_name"] = event.pageName;
    }
    if (!event.content.is_null()) {
        json["content"] = event.content;
    }else{
        json["content"] = nlohmann::json::object();
    }
    return json;
}

} // namespace Slic3r
