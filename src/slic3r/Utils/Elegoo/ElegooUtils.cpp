#include "ElegooUtils.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <fstream>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace Slic3r {
namespace {

std::string md5HexLower(const std::string& input)
{
    using boost::uuids::detail::md5;

    md5              md5Hash;
    md5::digest_type digest{};
    std::string      digestString;

    md5Hash.process_bytes(input.data(), input.size());
    md5Hash.get_digest(digest);
    boost::algorithm::hex(digest, digest + std::size(digest), std::back_inserter(digestString));
    boost::to_lower(digestString);
    return digestString;
}

std::string generateSessionId()
{
    std::string sessionId = boost::uuids::to_string(boost::uuids::random_generator()());
    boost::erase_all(sessionId, "-");
    return sessionId;
}

std::string getMachineIdRaw()
{
    std::string machineId;

#ifdef _WIN32
    HKEY registryKey = nullptr;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &registryKey);
    if (result == ERROR_SUCCESS && registryKey != nullptr) {
        char  data[256] = {0};
        DWORD dataSize  = sizeof(data) - 1;
        DWORD type      = 0;
        result = RegQueryValueExA(registryKey, "MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(data), &dataSize);
        if (result == ERROR_SUCCESS && type == REG_SZ && dataSize > 0) {
            data[dataSize] = '\0';
            machineId      = data;
        }
        RegCloseKey(registryKey);
    }

    if (machineId.empty()) {
        result = RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Elegoo\\Network", 0, KEY_READ, &registryKey);
        if (result == ERROR_SUCCESS && registryKey != nullptr) {
            char  data[256] = {0};
            DWORD dataSize  = sizeof(data) - 1;
            DWORD type      = 0;
            result = RegQueryValueExA(registryKey, "MachineId", nullptr, &type, reinterpret_cast<LPBYTE>(data), &dataSize);
            if (result == ERROR_SUCCESS && type == REG_SZ && dataSize > 0) {
                data[dataSize] = '\0';
                machineId      = data;
            }
            RegCloseKey(registryKey);
        }
    }

    if (machineId.empty()) {
        machineId = generateSessionId();
        HKEY userKey = nullptr;
        result = RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Elegoo\\Network", 0, nullptr, 0, KEY_WRITE, nullptr, &userKey, nullptr);
        if (result == ERROR_SUCCESS && userKey != nullptr) {
            RegSetValueExA(userKey, "MachineId", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(machineId.c_str()),
                           static_cast<DWORD>(machineId.size() + 1));
            RegCloseKey(userKey);
        }
    }
#elif defined(__APPLE__)
    #if defined(kIOMainPortDefault)
    io_service_t platformExpert = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    #else
    io_service_t platformExpert = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    #endif
    if (platformExpert) {
        CFStringRef serialRef = static_cast<CFStringRef>(IORegistryEntryCreateCFProperty(
            platformExpert, CFSTR("IOPlatformSerialNumber"), kCFAllocatorDefault, 0));
        if (serialRef != nullptr) {
            char serial[256];
            if (CFStringGetCString(serialRef, serial, sizeof(serial), kCFStringEncodingUTF8)) {
                machineId = serial;
            }
            CFRelease(serialRef);
        }

        if (machineId.empty()) {
            CFStringRef uuidRef = static_cast<CFStringRef>(IORegistryEntryCreateCFProperty(
                platformExpert, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0));
            if (uuidRef != nullptr) {
                char uuid[256];
                if (CFStringGetCString(uuidRef, uuid, sizeof(uuid), kCFStringEncodingUTF8)) {
                    machineId = uuid;
                }
                CFRelease(uuidRef);
            }
        }

        IOObjectRelease(platformExpert);
    }

    if (machineId.empty()) {
        struct utsname systemInfo;
        if (uname(&systemInfo) == 0) {
            machineId = std::string(systemInfo.sysname) + "_" + systemInfo.nodename + "_" + systemInfo.machine;
        }
    }
#else
    std::ifstream machineIdFile("/etc/machine-id");
    if (machineIdFile.is_open()) {
        std::getline(machineIdFile, machineId);
    }

    if (machineId.empty()) {
        std::ifstream dbusMachineIdFile("/var/lib/dbus/machine-id");
        if (dbusMachineIdFile.is_open()) {
            std::getline(dbusMachineIdFile, machineId);
        }
    }

    if (machineId.empty()) {
        struct utsname systemInfo;
        if (uname(&systemInfo) == 0) {
            machineId = std::string(systemInfo.sysname) + "_" + systemInfo.nodename + "_" + systemInfo.machine;
        }
        machineId += "_" + std::to_string(getpid());
    }
#endif

    if (machineId.empty()) {
        machineId = generateSessionId();
    }

    return machineId;
}

} // namespace

std::string ElegooUtils::getDeviceId()
{
    static std::string cachedDeviceId = md5HexLower(getMachineIdRaw());
    return cachedDeviceId;
}

} // namespace Slic3r