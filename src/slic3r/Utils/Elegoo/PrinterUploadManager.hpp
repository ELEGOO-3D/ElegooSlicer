#pragma once
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
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

    void addUploadTask(const std::string& taskId, const std::shared_ptr<UploadTaskData>& taskData);
    std::shared_ptr<UploadTaskData> getUploadTaskData(const std::string& taskId);
    std::vector<std::shared_ptr<UploadTaskData>> getAllUploadTasks();
    bool updateUploadTaskStatus(const std::string& taskId, UploadTaskStatus status,
                                PrinterNetworkErrorCode code = PrinterNetworkErrorCode::SUCCESS,
                                const std::string& message = std::string());
    void updateUploadTaskProgress(const std::string& taskId, uint64_t uploaded, uint64_t total, bool& cancel);
    void deleteUploadTask(const std::string& taskId, const std::shared_ptr<UploadTaskData>& expectedTaskData);

    PrinterNetworkResult<bool> slaveUpload(const PrinterNetworkParams& params);
    PrinterNetworkResult<bool> executeUpload(const PrinterNetworkParams& params);

    PrinterUploadManager();
};

} // namespace Slic3r
