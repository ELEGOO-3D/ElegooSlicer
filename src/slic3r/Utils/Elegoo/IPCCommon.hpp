#pragma once

#include "libslic3r/Utils.hpp"
#include <boost/filesystem.hpp>
#include <string>

namespace Slic3r {

constexpr size_t IPC_MAX_MESSAGE_SIZE           = 100 * 1024 * 1024;
constexpr size_t IPC_MAX_PENDING_WRITES         = 20;
constexpr int    IPC_REQUEST_TIMEOUT_SECONDS    = 30;
constexpr int    IPC_RECONNECT_INTERVAL_SECONDS = 1;

inline std::string getIPCPortFilePath()
{
    boost::filesystem::path cacheDir = boost::filesystem::path(data_dir()) / "cache";
    if (!boost::filesystem::exists(cacheDir)) {
        boost::filesystem::create_directories(cacheDir);
    }
    return (cacheDir / "ipc_port.txt").string();
}

} // namespace Slic3r
