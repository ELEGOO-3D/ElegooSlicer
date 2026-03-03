#pragma once
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "slic3r/Utils/Singleton.hpp"

namespace Slic3r {

class PrinterUploadManager : public Singleton<PrinterUploadManager>
{
    friend class Singleton<PrinterUploadManager>;
public:
    ~PrinterUploadManager();
    
    PrinterUploadManager(const PrinterUploadManager&) = delete;
    PrinterUploadManager& operator=(const PrinterUploadManager&) = delete;

    void init();
    void close();

    PrinterNetworkResult<bool> upload(const PrinterNetworkParams& params);
    PrinterNetworkResult<std::string> startAsyncUpload(const PrinterNetworkParams& params);
    PrinterNetworkResult<UploadTaskInfo> getUploadTask(const std::string& taskId);
    PrinterNetworkResult<bool> cancelUploadTask(const std::string& taskId);

private:
    struct UploadTaskData {
        UploadTaskInfo info;
        std::thread thread;
    };

    std::atomic<bool> mIsInitialized;

    std::map<std::string, std::shared_ptr<UploadTaskData>> mUploadTasks;
    std::mutex mUploadTasksMutex;

    void stopAllUploadTasks();
    PrinterNetworkResult<bool> slaveUpload(const PrinterNetworkParams& params);
    PrinterNetworkResult<bool> executeUpload(const PrinterNetworkParams& params);

    PrinterUploadManager();
};

} // namespace Slic3r
