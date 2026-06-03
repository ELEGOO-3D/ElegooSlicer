#include "TelemetryReporter.hpp"

#include "slic3r/Utils/Elegoo/ElegooUtils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

#include <GL/glew.h>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <wx/platinfo.h>
#include <wx/display.h>
#include <wx/utils.h>

#include "PrinterNetwork.hpp"
#include "UserNetworkManager.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/Utils/Http.hpp"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <cerrno>
#include <signal.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/utsname.h>
#endif

namespace Slic3r {
namespace {
namespace fs = boost::filesystem;

constexpr const char* kSdkVersion               = "1.0.0";
constexpr const char* kAppName                  = "elegoo-slicer";
constexpr const char* kAppId                    = "7";
constexpr size_t      kBatchSize                = 20;
constexpr long        kFlushIntervalMs          = 1000;
constexpr long        kRuntimeStatePersistIntervalSecond = 30;
constexpr long        kHttpConnectTimeoutSecond = 5;
constexpr long        kHttpTimeoutSecond        = 10;
constexpr long        kMaxRetrySkipCount        = 300;
constexpr const char* kTelemetryRuntimeStateDirectoryName = "runtime_state";
constexpr const char* kPendingEventsFilePrefix  = "pending_events_";
constexpr const char* kPendingEventsFileSuffix  = ".jsonl";
constexpr size_t      kMaxLinesToLoadPerCacheFile = 1000;
constexpr size_t      kMaxCacheMemoryBytes      = 5 * 1024 * 1024;
constexpr size_t      kEstimatedEventSize       = 500;

#ifdef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
constexpr GLenum kGpuMemoryInfoTotalAvailableMemoryNvx = GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX;
#else
constexpr GLenum kGpuMemoryInfoTotalAvailableMemoryNvx = 0x9048;
#endif

#ifdef GL_TEXTURE_FREE_MEMORY_ATI
constexpr GLenum kTextureFreeMemoryAti = GL_TEXTURE_FREE_MEMORY_ATI;
#else
constexpr GLenum kTextureFreeMemoryAti = 0x87FC;
#endif

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

std::string normalizeArchName(std::string arch)
{
    const std::string lowered = boost::algorithm::to_lower_copy(boost::algorithm::trim_copy(arch));
    if (lowered == "amd64" || lowered == "x64" || lowered == "x86-64" || lowered == "64 bit") {
        return "x86_64";
    }
    if (lowered == "x86" || lowered == "i386" || lowered == "i686") {
        return "x86";
    }
    if (lowered == "aarch64") {
        return "arm64";
    }
    return arch;
}

std::string buildWindowsReleaseName(int major, int minor, int build)
{
#ifdef _WIN32
    if (major == 10) {
        return build >= 22000 ? "Windows 11" : "Windows 10";
    }
    if (major > 0) {
        return "Windows " + std::to_string(major) + "." + std::to_string(minor);
    }
    return "Windows";
#else
    (void)major;
    (void)minor;
    (void)build;
    return std::string();
#endif
}

std::string buildOsName()
{
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

std::string buildOsDisplayName()
{
#ifdef _WIN32
    int major = 0;
    int minor = 0;
    int build = 0;
    wxGetOsVersion(&major, &minor, &build);
    return buildWindowsReleaseName(major, minor, build);
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

std::string buildOsVersion()
{
#ifdef _WIN32
    int major = 0;
    int minor = 0;
    int build = 0;
    wxGetOsVersion(&major, &minor, &build);

    std::string version = buildWindowsReleaseName(major, minor, build);
    if (major > 0) {
        version += " / " + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(build);
    }
    return version;
#elif defined(__APPLE__)
    wxPlatformInfo platformInfo;
    const int major = platformInfo.GetOSMajorVersion();
    const int minor = platformInfo.GetOSMinorVersion();
    const int micro = platformInfo.GetOSMicroVersion();

    if (major <= 0) {
        return std::string();
    }

    std::string version = std::to_string(major) + "." + std::to_string(minor);
    if (micro > 0) {
        version += "." + std::to_string(micro);
    }
    return version;
#else
    struct utsname systemInfo;
    if (uname(&systemInfo) == 0) {
        return systemInfo.release;
    }
    return std::string();
#endif
}

std::string getOsArch()
{
    return normalizeArchName(wxPlatformInfo::Get().GetArchName().ToUTF8().data());
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

unsigned int getLogicalCoreCount()
{
    return std::thread::hardware_concurrency();
}

#ifdef _WIN32
std::string getCpuModelFromRegistry()
{
    int idx = -1;
    constexpr DWORD kBufferSize = 512;
    DWORD bufSize = kBufferSize - 1;
    char buffer[kBufferSize] = {};
    const std::string basePath = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\";
    std::string regPath = basePath;

    while (true) {
        if (RegGetValueA(HKEY_LOCAL_MACHINE, regPath.c_str(), "ProcessorNameString", RRF_RT_REG_SZ, nullptr, &buffer, &bufSize) == ERROR_SUCCESS) {
            return std::string(buffer);
        }

        if (idx >= 0) {
            break;
        }

        ++idx;
        regPath = basePath + std::to_string(idx) + "\\";
        bufSize = kBufferSize - 1;
        std::memset(buffer, 0, sizeof(buffer));
    }

    return std::string();
}

std::string normalizeDisplayDeviceRegistryPath(std::string path)
{
    const std::string prefix = "\\Registry\\Machine\\";
    if (boost::algorithm::istarts_with(path, prefix)) {
        path.erase(0, prefix.size());
    }
    return path;
}

bool readRegistryString(HKEY keyRoot, const std::string& subKey, const char* valueName, std::string& value)
{
    char buffer[512] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueA(keyRoot, subKey.c_str(), valueName, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return false;
    }

    value.assign(buffer);
    return true;
}

bool readRegistryQword(HKEY keyRoot, const std::string& subKey, const char* valueName, uint64_t& value)
{
    ULONGLONG qwordValue = 0;
    DWORD size = sizeof(qwordValue);
    if (RegGetValueA(keyRoot, subKey.c_str(), valueName, RRF_RT_REG_QWORD, nullptr, &qwordValue, &size) == ERROR_SUCCESS) {
        value = static_cast<uint64_t>(qwordValue);
        return true;
    }

    DWORD dwordValue = 0;
    size = sizeof(dwordValue);
    if (RegGetValueA(keyRoot, subKey.c_str(), valueName, RRF_RT_REG_DWORD, nullptr, &dwordValue, &size) == ERROR_SUCCESS) {
        value = static_cast<uint64_t>(dwordValue);
        return true;
    }

    return false;
}

void getWindowsPrimaryGpuInfo(std::string& gpuModel, std::string& gpuDriverVersion, uint64_t& gpuVramMb)
{
    DISPLAY_DEVICEA displayDevice{};
    displayDevice.cb = sizeof(displayDevice);

    for (DWORD index = 0; EnumDisplayDevicesA(nullptr, index, &displayDevice, 0); ++index) {
        if ((displayDevice.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
            displayDevice.cb = sizeof(displayDevice);
            continue;
        }

        if (gpuModel.empty()) {
            gpuModel = displayDevice.DeviceString;
        }

        const std::string registryPath = normalizeDisplayDeviceRegistryPath(displayDevice.DeviceKey);
        if (!registryPath.empty()) {
            if (gpuDriverVersion.empty()) {
                readRegistryString(HKEY_LOCAL_MACHINE, registryPath, "DriverVersion", gpuDriverVersion);
            }

            if (gpuVramMb == 0) {
                uint64_t gpuMemoryBytes = 0;
                if (readRegistryQword(HKEY_LOCAL_MACHINE, registryPath, "HardwareInformation.qwMemorySize", gpuMemoryBytes) ||
                    readRegistryQword(HKEY_LOCAL_MACHINE, registryPath, "HardwareInformation.MemorySize", gpuMemoryBytes)) {
                    gpuVramMb = gpuMemoryBytes / (1024ull * 1024ull);
                }
            }
        }

        if ((displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0) {
            break;
        }

        displayDevice.cb = sizeof(displayDevice);
    }
}
#elif defined(__APPLE__)
std::string getSysctlString(const char* name)
{
    size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return std::string();
    }

    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) {
        return std::string();
    }

    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}
#endif

std::string getCpuModel()
{
#ifdef _WIN32
    return getCpuModelFromRegistry();
#elif defined(__APPLE__)
    std::string model = getSysctlString("machdep.cpu.brand_string");
    if (model.empty()) {
        model = getSysctlString("hw.model");
    }
    return model;
#else
    std::ifstream cpuInfo("/proc/cpuinfo");
    if (!cpuInfo.is_open()) {
        return std::string();
    }

    std::string line;
    while (std::getline(cpuInfo, line)) {
        const size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, separator);
        boost::algorithm::trim(key);
        if (key != "model name" && key != "Hardware") {
            continue;
        }

        std::string value = line.substr(separator + 1);
        boost::algorithm::trim(value);
        return value;
    }
    return std::string();
#endif
}

double getTotalMemoryGb()
{
    const double bytes = static_cast<double>(Slic3r::total_physical_memory());
    if (bytes <= 0.0) {
        return 0.0;
    }

    const double gib = bytes / (1024.0 * 1024.0 * 1024.0);
    return std::round(gib * 10.0) / 10.0;
}

struct PersistedRuntimeState {
    unsigned    processId{0};
    std::string sessionId;
    int64_t     runtimeSec{0};

    bool valid() const
    {
        return processId != 0 && !sessionId.empty() && runtimeSec > 0;
    }
};

std::string getTelemetryCacheDirectoryUtf8()
{
    // data_dir() is stored as UTF-8, so keep the path in UTF-8 for nowide I/O.
    std::string path = Slic3r::data_dir();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    path += "user/telemetry_cache/";
    return path;
}

fs::path makeFilesystemPathFromUtf8(const std::string& utf8Path)
{
#ifdef _WIN32
    return fs::path(boost::nowide::widen(utf8Path));
#else
    return fs::path(utf8Path);
#endif
}

fs::path getTelemetryCacheDirectory()
{
    return makeFilesystemPathFromUtf8(getTelemetryCacheDirectoryUtf8());
}

std::string pathToUtf8(const fs::path& path)
{
#ifdef _WIN32
    return boost::nowide::narrow(path.wstring());
#else
    return path.string();
#endif
}

std::string getTelemetryRuntimeStateDirectoryUtf8()
{
    std::string path = getTelemetryCacheDirectoryUtf8();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    path += kTelemetryRuntimeStateDirectoryName;
    path += '/';
    return path;
}

fs::path getTelemetryRuntimeStateDirectory()
{
    return makeFilesystemPathFromUtf8(getTelemetryRuntimeStateDirectoryUtf8());
}

std::string getTelemetryStateFilePathUtf8(unsigned processId)
{
    return getTelemetryRuntimeStateDirectoryUtf8() + std::to_string(processId) + ".json";
}

fs::path getTelemetryStateFilePath(unsigned processId)
{
    return makeFilesystemPathFromUtf8(getTelemetryStateFilePathUtf8(processId));
}

std::tm getLocalTime(std::time_t timestamp)
{
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    return localTime;
}

/**
 * Computes the next local midnight from the given time point.
 */
std::chrono::system_clock::time_point computeNextLocalMidnight(std::chrono::system_clock::time_point now)
{
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = getLocalTime(nowTime);
    localTime.tm_mday += 1;
    localTime.tm_hour = 0;
    localTime.tm_min = 0;
    localTime.tm_sec = 0;
    const std::time_t midnightTime = std::mktime(&localTime);
    if (midnightTime <= 0) {
        return now + std::chrono::hours(24);
    }

    return std::chrono::system_clock::from_time_t(midnightTime);
}

bool isProcessAlive(unsigned processId)
{
#ifdef _WIN32
    HANDLE processHandle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (processHandle == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }

    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(processHandle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(processHandle);
    return alive;
#else
    if (processId == 0) {
        return false;
    }

    if (::kill(static_cast<pid_t>(processId), 0) == 0) {
        return true;
    }

    return errno == EPERM;
#endif
}

PersistedRuntimeState loadPersistedRuntimeState(unsigned processId)
{
    PersistedRuntimeState state;
    state.processId = processId;

    const std::string stateFilePathUtf8 = getTelemetryStateFilePathUtf8(processId);
    const fs::path stateFilePath = getTelemetryStateFilePath(processId);
    boost::system::error_code errorCode;
    if (!fs::exists(stateFilePath, errorCode) || errorCode) {
        return state;
    }

    boost::nowide::ifstream input(stateFilePathUtf8, std::ios::binary);
    if (!input.is_open()) {
        return state;
    }

    const nlohmann::json json = nlohmann::json::parse(input, nullptr, false, true);
    if (json.is_discarded()) {
        return state;
    }

    state.processId = json.value("process_id", processId);
    state.sessionId = json.value("session_id", std::string());
    state.runtimeSec = json.value("runtime_sec", int64_t{0});
    return state;
}

void clearPersistedRuntimeState(unsigned processId)
{
    boost::system::error_code errorCode;
    fs::remove(getTelemetryStateFilePath(processId), errorCode);
}

/**
 * Persists the current runtime state to a file.
 */
void persistRuntimeState(unsigned processId, const std::string& sessionId, int64_t runtimeSec)
{
    if (processId == 0 || sessionId.empty() || runtimeSec <= 0) {
        clearPersistedRuntimeState(processId);
        return;
    }

    const std::string stateDirUtf8 = getTelemetryRuntimeStateDirectoryUtf8();
    const std::string stateFilePathUtf8 = getTelemetryStateFilePathUtf8(processId);
    const fs::path stateDirPath = getTelemetryRuntimeStateDirectory();
    boost::system::error_code errorCode;
    fs::create_directories(stateDirPath, errorCode);
    if (errorCode) {
        BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to create runtime state directory, error=" << errorCode.message();
        return;
    }

    boost::nowide::ofstream output(stateFilePathUtf8, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to open runtime state file for writing";
        return;
    }

    nlohmann::json json = nlohmann::json::object();
    json["process_id"] = processId;
    json["session_id"] = sessionId;
    json["runtime_sec"] = runtimeSec;
    output << json.dump();
}

std::vector<std::string> loadCachedEventsFromFile(const std::string& filePath)
{
    std::vector<std::string> cachedEvents;
    boost::nowide::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return cachedEvents;
    }

    std::string line;
    while (std::getline(input, line) && cachedEvents.size() < kMaxLinesToLoadPerCacheFile) {
        if (line.empty()) {
            continue;
        }
        cachedEvents.push_back(std::move(line));
    }

    return cachedEvents;
}

void appendCachedEventsWithLimit(std::deque<std::string>& queue, std::vector<std::string>& cachedEvents)
{
    for (std::string& jsonStr : cachedEvents) {
        while (queue.size() * kEstimatedEventSize >= kMaxCacheMemoryBytes && !queue.empty()) {
            queue.pop_front();
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: cache memory limit reached, dropping oldest event";
        }
        queue.push_back(std::move(jsonStr));
    }
}

} // namespace

TelemetryReporter::TelemetryReporter() = default;

TelemetryReporter::~TelemetryReporter()
{
    stop();
}

void TelemetryReporter::init()
{
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        if (!mInitialized.load(std::memory_order_acquire)) {
            initializeStaticContextLocked();
            mInitialized.store(true, std::memory_order_release);
        }

        // Always start worker thread
        if (!mWorkerRunning.load(std::memory_order_acquire)) {
            startWorkerLocked();
        }
    }
}

void TelemetryReporter::stop()
{
    std::string sessionId;
    int64_t runtimeSec = 0;
    unsigned processId = 0;
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        processId = mProcessId;
        sessionId = mSessionId;
        runtimeSec = getCurrentRuntimeSecLocked(std::chrono::system_clock::now());
        mAccepting.store(false, std::memory_order_release);
    }
    mQueueCv.notify_all();

    if (mWorkerThread.joinable()) {
        mWorkerThread.join();
    }

    persistRuntimeState(processId, sessionId, runtimeSec);

    mWorkerRunning.store(false, std::memory_order_release);
    mInitialized.store(false, std::memory_order_release);
}

void TelemetryReporter::updateGraphicsContext()
{
    if (!mInitialized.load(std::memory_order_acquire)) {
        init();
    }

    std::lock_guard<std::mutex> lock(mStateMutex);
    updateGraphicsContextLocked();
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

    PendingEvent event = buildPendingEvent(eventName, content, pageName);
    std::string serializedJson = serializeEvent(event).dump();

    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        while (mQueue.size() * kEstimatedEventSize >= kMaxCacheMemoryBytes && !mQueue.empty()) {
            mQueue.pop_front();
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: cache memory limit reached, dropping oldest event";
        }
        mQueue.push_back(std::move(serializedJson));
        mCacheDirty.store(true, std::memory_order_release);
    }
    mQueueCv.notify_one();
}

void TelemetryReporter::initializeStaticContextLocked()
{
    const auto now = std::chrono::system_clock::now();
    mSessionId      = generateSessionId();
    mDeviceId       = ElegooUtils::getDeviceId();
    mPlatform       = normalizePlatform();
    mProcessId      = get_current_pid();
    mSessionStartedAt = now;
    mNextRuntimeReportAt = computeNextLocalMidnight(now);
    // Keep a recent runtime snapshot on disk so another process can recover and
    // report it once this process is no longer alive.
    mNextRuntimeStatePersistAt = now + std::chrono::seconds(kRuntimeStatePersistIntervalSecond);

    mDeviceInfo.os               = buildOsDisplayName();
    mDeviceInfo.osName           = buildOsName();
    mDeviceInfo.osVersion        = buildOsVersion();
    mDeviceInfo.osArch           = getOsArch();
    mDeviceInfo.screenResolution = getScreenResolution();
    mDeviceInfo.appLanguage      = GUI::wxGetApp().app_config ? GUI::wxGetApp().app_config->get_language_code() : std::string("en");
    mDeviceInfo.systemLanguage   = GUI::wxGetApp().app_config ? GUI::wxGetApp().app_config->getSystemLanguage() : std::string("en");
    mDeviceInfo.cpuModel         = getCpuModel();
    mDeviceInfo.cpuLogicalCores  = getLogicalCoreCount();
    mDeviceInfo.memoryTotalGb    = getTotalMemoryGb();

#ifdef _WIN32
    getWindowsPrimaryGpuInfo(mDeviceInfo.gpuModel, mDeviceInfo.gpuDriverVersion, mDeviceInfo.gpuVramMb);
#endif

    auto networkHelper = NetworkFactory::createNetworkHelper(htElegooLink);
    if (networkHelper) {
        mTelemetryUrl = networkHelper->getTelemetryUrl();
        mUserAgent  = networkHelper->getUserAgent();
    }
}

void TelemetryReporter::updateGraphicsContextLocked()
{
    const GUI::OpenGLManager::GLInfo& glInfo = GUI::OpenGLManager::get_gl_info();

    mDeviceInfo.openglVersion  = glInfo.get_version();
    mDeviceInfo.openglVendor   = glInfo.get_vendor();
    mDeviceInfo.openglRenderer = glInfo.get_renderer();

    if (mDeviceInfo.openglVersion == "N/A") {
        mDeviceInfo.openglVersion.clear();
    }
    if (mDeviceInfo.openglVendor == "N/A") {
        mDeviceInfo.openglVendor.clear();
    }
    if (mDeviceInfo.openglRenderer == "N/A") {
        mDeviceInfo.openglRenderer.clear();
    }

    if (mDeviceInfo.gpuModel.empty()) {
        mDeviceInfo.gpuModel = mDeviceInfo.openglRenderer;
    }

    if (mDeviceInfo.gpuVramMb == 0) {
        if (GLEW_NVX_gpu_memory_info) {
            GLint totalMemoryKb = 0;
            glGetIntegerv(kGpuMemoryInfoTotalAvailableMemoryNvx, &totalMemoryKb);
            if (totalMemoryKb > 0) {
                mDeviceInfo.gpuVramMb = static_cast<uint64_t>(totalMemoryKb) / 1024ull;
            }
        } else if (GLEW_ATI_meminfo) {
            GLint freeMemoryKb[4] = { 0, 0, 0, 0 };
            glGetIntegerv(kTextureFreeMemoryAti, freeMemoryKb);
            if (freeMemoryKb[0] > 0) {
                mDeviceInfo.gpuVramMb = static_cast<uint64_t>(freeMemoryKb[0]) / 1024ull;
            }
        }
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

void TelemetryReporter::reportPendingAppRuntimeOnStartup()
{
    const fs::path stateDirectory = getTelemetryRuntimeStateDirectory();
    boost::system::error_code errorCode;
    if (!fs::exists(stateDirectory, errorCode) || errorCode) {
        return;
    }

    std::vector<PersistedRuntimeState> staleStates;
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        for (fs::directory_iterator iterator(stateDirectory, errorCode), end; !errorCode && iterator != end; iterator.increment(errorCode)) {
            if (!fs::is_regular_file(iterator->status())) {
                continue;
            }

            const std::string stem = iterator->path().stem().string();
            unsigned processId = 0;
            try {
                processId = static_cast<unsigned>(std::stoul(stem));
            } catch (const std::exception&) {
                fs::remove(iterator->path(), errorCode);
                errorCode.clear();
                continue;
            }

            if (processId == 0 || processId == mProcessId || isProcessAlive(processId)) {
                continue;
            }

            PersistedRuntimeState state = loadPersistedRuntimeState(processId);
            if (!state.valid()) {
                clearPersistedRuntimeState(processId);
                continue;
            }

            staleStates.push_back(std::move(state));
        }
    }

    for (const PersistedRuntimeState& state : staleStates) {
        reportEvent("app_runtime", buildAppRuntimePayload(0, state.sessionId, state.runtimeSec));
        clearPersistedRuntimeState(state.processId);
    }
}

void TelemetryReporter::reportAppRuntimeOnDayBoundary()
{
    nlohmann::json payload;
    bool shouldReport = false;
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        if (!mAccepting.load(std::memory_order_acquire)) {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        if (mNextRuntimeReportAt.time_since_epoch().count() == 0 || now < mNextRuntimeReportAt) {
            return;
        }

        // Emit one runtime sample when the local day rolls over so long-running
        // sessions contribute a daily app_runtime event without waiting for exit.
        payload = buildAppRuntimePayload(getCurrentRuntimeSecLocked(now), std::string(), 0);
        mNextRuntimeReportAt = computeNextLocalMidnight(now);
        shouldReport = true;
    }

    if (shouldReport) {
        reportEvent("app_runtime", payload);
    }
}

void TelemetryReporter::persistRuntimeStatePeriodically()
{
    bool shouldPersist = false;
    unsigned processId = 0;
    std::string sessionId;
    int64_t runtimeSec = 0;
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        if (!mAccepting.load(std::memory_order_acquire)) {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        if (mNextRuntimeStatePersistAt.time_since_epoch().count() == 0 || now < mNextRuntimeStatePersistAt) {
            return;
        }

        sessionId = mSessionId;
        processId = mProcessId;
        runtimeSec = getCurrentRuntimeSecLocked(now);
        mNextRuntimeStatePersistAt = now + std::chrono::seconds(kRuntimeStatePersistIntervalSecond);
        shouldPersist = true;
    }

    if (shouldPersist) {
        persistRuntimeState(processId, sessionId, runtimeSec);
    }
}

void TelemetryReporter::runWorker()
{
    bool pendingRuntimeReported = false;
    bool cachedEventsLoaded = false;

    while (mAccepting.load(std::memory_order_acquire)) {
        if (!cachedEventsLoaded) {
            recoverDeadProcessCachedEventsOnStartup();
            loadCachedEvents();
            cachedEventsLoaded = true;
        }

        reportAppRuntimeOnDayBoundary();
        persistRuntimeStatePeriodically();

        if (!pendingRuntimeReported) {
            reportPendingAppRuntimeOnStartup();
            pendingRuntimeReported = true;
        }

        backupCacheToFile();

        // Exponential backoff: skip upload attempts after failures
        if (mRetrySkipCount > 0) {
            --mRetrySkipCount;
        }

        if (!isUserLoggedIn()) {
            // Not logged in — wait with CV so stop() can wake us up immediately
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mQueueCv.wait_for(lock, std::chrono::milliseconds(kFlushIntervalMs), [this]() {
                return !mAccepting.load(std::memory_order_acquire);
            });
            if (!mAccepting.load(std::memory_order_acquire)) {
                break;
            }
            continue;
        }

        // Logged in — wait for events or shutdown
        std::vector<std::string> batch;
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            mQueueCv.wait_for(lock, std::chrono::milliseconds(kFlushIntervalMs), [this]() {
                return !mAccepting.load(std::memory_order_acquire)
                       || (mRetrySkipCount <= 0 && !mQueue.empty());
            });

            if (!mAccepting.load(std::memory_order_acquire)) {
                break;
            }

            if (mRetrySkipCount > 0 || mQueue.empty()) {
                continue;
            }

            const size_t batchCount = std::min(kBatchSize, mQueue.size());
            batch.reserve(batchCount);
            for (size_t index = 0; index < batchCount; ++index) {
                batch.push_back(std::move(mQueue.front()));
                mQueue.pop_front();
            }
        }

        // Upload batch
        if (sendBatch(batch)) {
            mRetrySkipCount = 0;
            mCacheDirty.store(true, std::memory_order_release);
            BOOST_LOG_TRIVIAL(debug) << "TelemetryReporter: successfully uploaded " << batch.size() << " events";
        } else {
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to upload " << batch.size() << " events, will retry";
            std::lock_guard<std::mutex> lock(mQueueMutex);
            for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                mQueue.push_front(std::move(*it));
            }
            mCacheDirty.store(true, std::memory_order_release);

            // Exponential backoff: 1, 2, 4, 8, ... loop iterations (each ~1s), capped
            mRetrySkipCount = mRetrySkipCount == 0 ? 1 : std::min(mRetrySkipCount * 2, kMaxRetrySkipCount);
            BOOST_LOG_TRIVIAL(info) << "TelemetryReporter: will skip " << mRetrySkipCount << " upload attempts before retry";
        }
    }

    backupCacheToFile();
}

bool TelemetryReporter::sendBatch(const std::vector<std::string>& batch)
{
    if (batch.empty() || mTelemetryUrl.empty()) {
        return false;
    }

    // Build JSON array from pre-serialized strings, skipping any corrupted entries
    nlohmann::json payload = nlohmann::json::array();
    for (const std::string& jsonStr : batch) {
        try {
            payload.push_back(nlohmann::json::parse(jsonStr));
        } catch (const nlohmann::json::parse_error& e) {
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: skipping invalid JSON event: " << e.what();
            continue;
        }
    }

    if (payload.empty()) {
        return true;  // All events were invalid, consider as success to clear them
    }

    std::string payloadStr = payload.dump();
    std::string path = "/api/v1/event-report-server/events";
    if (mTelemetryUrl.back() == '/') {
        path = "api/v1/event-report-server/events";
    }
    BOOST_LOG_TRIVIAL(debug) << "TelemetryReporter: uploading batch, size=" << payload.size() << ", payload=" << payloadStr;

    bool success = true;
    Http http = Http::post(mTelemetryUrl + path);
    http.header("Content-Type", "application/json")
        .timeout_connect(kHttpConnectTimeoutSecond)
        .timeout_max(kHttpTimeoutSecond)
        .set_post_body(payloadStr)
        .on_complete([&success](std::string body, unsigned httpStatus) {
            success = httpStatus >= 200 && httpStatus < 300;
        })
        .on_error([&success](std::string body, std::string error, unsigned httpStatus) {
            success = false;
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: upload error, status=" << httpStatus
                                       << ", error=" << error;
        });

    if (!mUserAgent.empty()) {
        http.header("User-Agent", mUserAgent);
    }

    http.perform_sync();
    return success;
}

nlohmann::json TelemetryReporter::buildAppRuntimePayload(int64_t runtimeSec,
                                                         const std::string& lastSessionId,
                                                         int64_t lastSessionRuntimeSec) const
{
    nlohmann::json content = nlohmann::json::object();
    content["runtime_sec"] = std::max<int64_t>(runtimeSec, 0);
    content["last_session_id"] = lastSessionId;
    content["last_session_runtime_sec"] = std::max<int64_t>(lastSessionRuntimeSec, 0);
    return content;
}

int64_t TelemetryReporter::getCurrentRuntimeSecLocked(std::chrono::system_clock::time_point now) const
{
    if (mSessionStartedAt.time_since_epoch().count() == 0) {
        return 0;
    }

    return std::max<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - mSessionStartedAt).count(),
        0);
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
    DeviceInfoSnapshot deviceSnapshot;
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        deviceSnapshot = mDeviceInfo;
    }

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
    deviceInfo["os"] = deviceSnapshot.os;
    deviceInfo["os_name"] = deviceSnapshot.osName;
    deviceInfo["os_version"] = deviceSnapshot.osVersion;
    deviceInfo["os_arch"] = deviceSnapshot.osArch;
    deviceInfo["cpu_model"] = deviceSnapshot.cpuModel;
    deviceInfo["cpu_logical_cores"] = deviceSnapshot.cpuLogicalCores;
    deviceInfo["gpu_model"] = deviceSnapshot.gpuModel;
    deviceInfo["gpu_driver_version"] = deviceSnapshot.gpuDriverVersion;
    deviceInfo["gpu_vram_mb"] = deviceSnapshot.gpuVramMb;
    deviceInfo["memory_total_gb"] = deviceSnapshot.memoryTotalGb;
    deviceInfo["opengl_version"] = deviceSnapshot.openglVersion;
    deviceInfo["opengl_vendor"] = deviceSnapshot.openglVendor;
    deviceInfo["opengl_renderer"] = deviceSnapshot.openglRenderer;
    if (!deviceSnapshot.screenResolution.empty()) {
        deviceInfo["screen_resolution"] = deviceSnapshot.screenResolution;
    }
    if (!deviceSnapshot.appLanguage.empty()) {
        deviceInfo["app_lang"] = deviceSnapshot.appLanguage;
    }
    if (!deviceSnapshot.systemLanguage.empty()) {
        deviceInfo["sys_lang"] = deviceSnapshot.systemLanguage;
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

bool TelemetryReporter::isUserLoggedIn() const
{
    UserNetworkInfo userInfo = UserNetworkManager::getInstance()->getUserInfo();
    return userInfo.loginStatus == LOGIN_STATUS_LOGIN_SUCCESS;
}

std::string TelemetryReporter::getCacheDirectory() const
{
    return getTelemetryCacheDirectoryUtf8();
}

std::string TelemetryReporter::getCacheFilePath() const
{
    return getCacheDirectory() + kPendingEventsFilePrefix + std::to_string(mProcessId) + kPendingEventsFileSuffix;
}

void TelemetryReporter::loadCachedEvents()
{
    try {
        const std::string filePath = getCacheFilePath();
        boost::system::error_code errorCode;
        if (!fs::exists(fs::path(filePath), errorCode) || errorCode) {
            return;
        }

        std::vector<std::string> cachedEvents = loadCachedEventsFromFile(filePath);

        if (!cachedEvents.empty()) {
            BOOST_LOG_TRIVIAL(info) << "TelemetryReporter: loading " << cachedEvents.size() << " cached events from file";
            {
                std::lock_guard<std::mutex> lock(mQueueMutex);
                appendCachedEventsWithLimit(mQueue, cachedEvents);
                mCacheDirty.store(true, std::memory_order_release);
            }
            mQueueCv.notify_one();
        }

        // Clear the cache file after loading
        boost::nowide::ofstream output(filePath, std::ios::trunc);
        output.close();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to load cached events, error=" << e.what();
    }
}

void TelemetryReporter::recoverDeadProcessCachedEventsOnStartup()
{
    const fs::path cacheDirectory = getTelemetryCacheDirectory();
    boost::system::error_code errorCode;
    if (!fs::exists(cacheDirectory, errorCode) || errorCode) {
        return;
    }

    std::vector<fs::path> staleCacheFiles;
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
        for (fs::directory_iterator iterator(cacheDirectory, errorCode), end; !errorCode && iterator != end; iterator.increment(errorCode)) {
            if (!fs::is_regular_file(iterator->status())) {
                continue;
            }

            const std::string fileName = iterator->path().filename().string();
            if (!boost::algorithm::starts_with(fileName, kPendingEventsFilePrefix) ||
                !boost::algorithm::ends_with(fileName, kPendingEventsFileSuffix)) {
                continue;
            }

            const size_t processIdStart = std::strlen(kPendingEventsFilePrefix);
            const size_t processIdLength = fileName.size() - processIdStart - std::strlen(kPendingEventsFileSuffix);
            if (processIdLength == 0) {
                continue;
            }

            unsigned processId = 0;
            try {
                processId = static_cast<unsigned>(std::stoul(fileName.substr(processIdStart, processIdLength)));
            } catch (const std::exception&) {
                continue;
            }

            if (processId == 0 || processId == mProcessId || isProcessAlive(processId)) {
                continue;
            }

            staleCacheFiles.push_back(iterator->path());
        }
    }

    for (const fs::path& cacheFilePath : staleCacheFiles) {
        try {
            const std::string filePathUtf8 =
#ifdef _WIN32
                boost::nowide::narrow(cacheFilePath.wstring());
#else
                cacheFilePath.string();
#endif
            std::vector<std::string> cachedEvents = loadCachedEventsFromFile(filePathUtf8);
            if (cachedEvents.empty()) {
                fs::remove(cacheFilePath, errorCode);
                errorCode.clear();
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mQueueMutex);
                appendCachedEventsWithLimit(mQueue, cachedEvents);
                mCacheDirty.store(true, std::memory_order_release);
            }
            mQueueCv.notify_one();

            BOOST_LOG_TRIVIAL(info) << "TelemetryReporter: recovered " << cachedEvents.size() << " cached events from dead process file " << filePathUtf8;
            fs::remove(cacheFilePath, errorCode);
            errorCode.clear();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to recover cached events from dead process file, error=" << e.what();
        }
    }
}

void TelemetryReporter::backupCacheToFile()
{
    // Only backup if cache has changed (atomic exchange)
    if (!mCacheDirty.exchange(false, std::memory_order_acquire)) {
        return;
    }

    try {
        const std::string cacheDir = getCacheDirectory();
        boost::system::error_code errorCode;
        fs::create_directories(fs::path(cacheDir), errorCode);
        if (errorCode) {
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to create cache directory, error=" << errorCode.message();
            return;
        }

        const std::string filePath = getCacheFilePath();
        boost::nowide::ofstream output(filePath, std::ios::trunc | std::ios::binary);
        if (!output.is_open()) {
            BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to open cache file for writing: " << filePath;
            return;
        }

        // Snapshot queue under lock, write file outside lock to avoid blocking reportEvent()
        std::deque<std::string> snapshot;
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            snapshot = mQueue;
        }

        for (const std::string& jsonStr : snapshot) {
            output << jsonStr << "\n";
        }
        output.close();

        BOOST_LOG_TRIVIAL(info) << "TelemetryReporter: backed up " << snapshot.size() << " events to cache file";
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "TelemetryReporter: failed to backup cache, error=" << e.what();
    }
}

} // namespace Slic3r
