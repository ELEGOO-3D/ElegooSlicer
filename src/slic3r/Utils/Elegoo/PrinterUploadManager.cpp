#include "PrinterUploadManager.hpp"
#include "PrinterManager.hpp"
#include "IPCClient.hpp"
#include "MultiInstanceCoordinator.hpp"
#include "PrinterCache.hpp"
#include "slic3r/Utils/Elegoo/UserNetworkManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <thread>

namespace Slic3r {

PrinterUploadManager::PrinterUploadManager()
    : mIsInitialized(false)
{
}

PrinterUploadManager::~PrinterUploadManager()
{
    close();
}

void PrinterUploadManager::init()
{
    mIsInitialized = true;
    BOOST_LOG_TRIVIAL(info) << "PrinterUploadManager::init";
}

void PrinterUploadManager::close()
{
    BOOST_LOG_TRIVIAL(info) << "PrinterUploadManager::close";
    mIsInitialized = false;
    stopAllUploadTasks();
}

PrinterNetworkResult<bool> PrinterUploadManager::upload(const PrinterNetworkParams& params)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return slaveUpload(params);
    }
    
    return executeUpload(params);
}

PrinterNetworkResult<std::string> PrinterUploadManager::startAsyncUpload(const PrinterNetworkParams& params)
{
    std::string taskId = boost::uuids::to_string(boost::uuids::random_generator()());
    
    auto now = std::chrono::system_clock::now();
    UploadTaskInfo task;
    task.taskId = taskId;
    task.printerId = params.printerId;
    task.fileName = params.fileName;
    task.beginTime = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    task.progress = 0;
    task.status = UploadTaskStatus::UPLOADING;
    
    try {
        boost::filesystem::path path(params.filePath);
        task.totalBytes = boost::filesystem::file_size(path);
    } catch (...) {
        task.totalBytes = 0;
    }
    
    auto taskData = std::make_shared<UploadTaskData>();
    taskData->info = task;
    
    {
        std::lock_guard<std::mutex> lock(mUploadTasksMutex);
        mUploadTasks[taskId] = taskData;
    }
    
    auto paramsCopy = std::make_shared<PrinterNetworkParams>(params);
    taskData->thread = std::thread([this, taskId, taskData, paramsCopy]() {
        auto progressFn = [this, taskId](uint64_t uploaded, uint64_t total, bool& cancel) {
            if (!mIsInitialized.load()) {
                cancel = true;
                return;
            }
            
            std::lock_guard<std::mutex> lock(mUploadTasksMutex);
            auto it = mUploadTasks.find(taskId);
            if (it != mUploadTasks.end()) {
                it->second->info.uploadedBytes = uploaded;
                it->second->info.totalBytes = total;
                it->second->info.progress = total > 0 ? static_cast<int>((uploaded * 100) / total) : 0;
                cancel = (it->second->info.status == UploadTaskStatus::CANCELLED) || !mIsInitialized.load();
            }
        };
        
        std::string errorMessage;
        auto errorFn = [taskId, &errorMessage](const std::string& errorMsg) {
            BOOST_LOG_TRIVIAL(error) << "Upload task " << taskId << " error: " << errorMsg;
            errorMessage = errorMsg;
        };
        
        paramsCopy->uploadProgressFn = progressFn;
        paramsCopy->errorFn = errorFn;
        
        auto result = executeUpload(*paramsCopy);
        
        {
            std::lock_guard<std::mutex> lock(mUploadTasksMutex);
            auto it = mUploadTasks.find(taskId);
            if (it != mUploadTasks.end()) {
                auto now = std::chrono::system_clock::now();
                it->second->info.endTime = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();              
                if (result.isSuccess()) {
                    it->second->info.status = UploadTaskStatus::SUCCESS;
                    it->second->info.progress = 100;
                }  else {
                    it->second->info.status = UploadTaskStatus::FAILED;
                    it->second->info.code = result.code;
                    it->second->info.message = !errorMessage.empty() ? errorMessage : result.message;
                }
            }
        }
    });
    
    return PrinterNetworkResult<std::string>(PrinterNetworkErrorCode::SUCCESS, taskId);
}

PrinterNetworkResult<UploadTaskInfo> PrinterUploadManager::getUploadTask(const std::string& taskId)
{
    std::lock_guard<std::mutex> lock(mUploadTasksMutex);
    auto it = mUploadTasks.find(taskId);
    if (it != mUploadTasks.end()) {
        UploadTaskInfo info = it->second->info;
        
        // Only remove tasks that are truly finished (SUCCESS or FAILED)
        // CANCELLED tasks should be kept, as they may still be in progress
        if (it->second->info.status == UploadTaskStatus::SUCCESS || 
            it->second->info.status == UploadTaskStatus::FAILED) {
            if (it->second->thread.joinable()) {
                it->second->thread.join();
            }
            mUploadTasks.erase(it);
        }
        
        return PrinterNetworkResult<UploadTaskInfo>(PrinterNetworkErrorCode::SUCCESS, info);
    }
    
    UploadTaskInfo emptyTask;
    emptyTask.taskId = taskId;
    return PrinterNetworkResult<UploadTaskInfo>(PrinterNetworkErrorCode::UNKNOWN_ERROR, emptyTask, "task not found");
}

PrinterNetworkResult<bool> PrinterUploadManager::cancelUploadTask(const std::string& taskId)
{
    std::lock_guard<std::mutex> lock(mUploadTasksMutex);
    auto it = mUploadTasks.find(taskId);
    if (it != mUploadTasks.end()) {
        it->second->info.status = UploadTaskStatus::CANCELLED;
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
    }
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::UNKNOWN_ERROR, false, "task not found");
}

void PrinterUploadManager::stopAllUploadTasks()
{
    std::lock_guard<std::mutex> lock(mUploadTasksMutex);
    for (auto& [taskId, taskData] : mUploadTasks) {
        if (taskData->info.status == UploadTaskStatus::UPLOADING) {
            taskData->info.status = UploadTaskStatus::CANCELLED;
        }
    }
    
    for (auto& [taskId, taskData] : mUploadTasks) {
        if (taskData->thread.joinable()) {
            taskData->thread.join();
        }
    }
    
    mUploadTasks.clear();
}

PrinterNetworkResult<bool> PrinterUploadManager::slaveUpload(const PrinterNetworkParams& params)
{
    auto startResult = IPCClient::getInstance()->upload(params);
    
    if (startResult.isError()) {
        return PrinterNetworkResult<bool>(startResult.code, false, startResult.message);
    }
    
    std::string taskId = *startResult.data;
    
    while (true) {     
        auto taskResult = IPCClient::getInstance()->getUploadTask(taskId);
        if (taskResult.isError()) {
            return PrinterNetworkResult<bool>(taskResult.code, false, taskResult.message);
        }
        
        UploadTaskInfo task = *taskResult.data;

        if (task.status == UploadTaskStatus::SUCCESS) {
            if (params.uploadProgressFn) {
                bool cancel = false;
                params.uploadProgressFn(task.totalBytes, task.totalBytes, cancel);
            }
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
        }
        if (task.status == UploadTaskStatus::FAILED) {
            if (params.errorFn) {
                params.errorFn(task.message);
            }
            return PrinterNetworkResult<bool>(task.code, false, task.message);
        }
        if (task.status == UploadTaskStatus::CANCELLED) {
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::OPERATION_CANCELLED, false);
        }
        
        if (params.uploadProgressFn) {
            bool cancel = false;
            params.uploadProgressFn(task.uploadedBytes, task.totalBytes, cancel);
            if (cancel) {
                IPCClient::getInstance()->cancelUploadTask(taskId);
                return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::OPERATION_CANCELLED, false);
            }
        }
        
        if (!mIsInitialized.load()) {
            IPCClient::getInstance()->cancelUploadTask(taskId);
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::OPERATION_CANCELLED, false);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

PrinterNetworkResult<bool> PrinterUploadManager::executeUpload(const PrinterNetworkParams& params)
{
    auto printer = PrinterCache::getInstance()->getPrinter(params.printerId);
    PrinterNetworkResult<bool> result(PrinterNetworkErrorCode::SUCCESS, false);
    bool isSendPrintTaskFailed = false;
    
    do {
        if (!printer.has_value()) {
            BOOST_LOG_TRIVIAL(error) << "executeUpload: printer not found, file name: " << params.fileName;
            result.code = PrinterNetworkErrorCode::PRINTER_NOT_FOUND;
            break;
        }
        if (printer.value().connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
            BOOST_LOG_TRIVIAL(error) << "executeUpload: printer not connected, file name: " << params.fileName;
            result.code = PrinterNetworkErrorCode::PRINTER_CONNECTION_ERROR;
            break;
        }
        if (printer.value().networkType == NETWORK_TYPE_WAN) {
            try {
                boost::filesystem::path path(params.filePath);
                boost::uintmax_t fileSize = boost::filesystem::file_size(path);
                if (fileSize > 500 * 1024 * 1024) {
                    result = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::FILE_TOO_LARGE, false);
                    break;
                }
            } catch (const boost::filesystem::filesystem_error& e) {
                BOOST_LOG_TRIVIAL(error) << "executeUpload: failed to get file size, path: " << params.filePath << ", error: " << e.what();
                result = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::FILE_NOT_FOUND, false);
                break;
            }
        }
        
        std::shared_ptr<IPrinterNetwork> network = PrinterManager::getInstance()->getPrinterNetwork(params.printerId);
        if (!network) {
            BOOST_LOG_TRIVIAL(error) << "executeUpload: no network connection for printer: " << params.printerId;
            result = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
            break;
        }
        
        UserNetworkInfo requestUserInfo = UserNetworkManager::getInstance()->getUserInfo();
        result = network->sendPrintFile(params);
        
        if (result.isError()) {
            PrinterManager::getInstance()->checkUserAuthStatus(printer.value(), result, requestUserInfo);
            BOOST_LOG_TRIVIAL(error) << "executeUpload: failed to send print file to printer " 
                << network->getPrinterNetworkInfo().host << " " 
                << network->getPrinterNetworkInfo().printerName << " " 
                << result.message;
            break;
        }
        
        if (result.isSuccess()) {
            BOOST_LOG_TRIVIAL(info) << "executeUpload: file upload success " 
                << printer.value().host << " " 
                << printer.value().printerName << ", file: " << params.fileName;
            
            if (params.uploadAndStartPrint) {
                result = network->sendPrintTask(params);
                if (result.isError()) {
                    PrinterManager::getInstance()->checkUserAuthStatus(printer.value(), result, requestUserInfo);
                    isSendPrintTaskFailed = true;
                }
            }
        }
    } while (0);
    
    if (result.isError() && params.errorFn) {
        std::string errorMessage = isSendPrintTaskFailed ? _u8L("Send print task failed") : _u8L("Send print file failed");
        params.errorFn(errorMessage + ", " + result.message);
    }
    
    return result;
}

} // namespace Slic3r

