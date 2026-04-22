#pragma once

#include "Singleton.hpp"
#include "slic3r/Utils/IPCMessage.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"
#include <boost/asio.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <memory>
#include <future>
#include <map>

namespace Slic3r {

/**
 * IPC Client for slave process
 *
 * Thread-safe client with blocking public API. Supports concurrent calls from multiple threads.
 * Public API methods must NOT be called from I/O thread to prevent deadlock.
 */
class IPCClient : public Singleton<IPCClient>
{
    friend class Singleton<IPCClient>;

public:
    IPCClient(const IPCClient&)            = delete;
    IPCClient& operator=(const IPCClient&) = delete;

    void start();
    void stop();

    PrinterNetworkInfo                                    getPrinterNetworkInfo(const std::string& printerId);
    std::vector<PrinterNetworkInfo>                       getPrinterList();
    PrinterNetworkInfo                                    getSelectedPrinter(const std::string& printerModel, const std::string& printerId);
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> discoverPrinter();
    PrinterNetworkResult<bool>                            cancelBindPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool>                            addPrinter(PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool>                            deletePrinter(const std::string& printerId);
    PrinterNetworkResult<bool>                            updatePrinterName(const std::string& printerId, const std::string& name);
    PrinterNetworkResult<bool>                            updatePrinterHost(const std::string& printerId, const std::string& host);
    PrinterNetworkResult<bool>            updatePhysicalPrinter(const std::string& printerId, const PrinterNetworkInfo& printerInfo);
    PrinterNetworkResult<PrinterMmsGroup> getPrinterMmsInfo(const std::string& printerId);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<PrinterPrintFileResponse> getFileDetail(const std::string& printerId, const std::string& fileName);
    PrinterNetworkResult<PrinterPrintTaskResponse> getPrintTaskList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<PrinterExceptionResponse> getExceptionList(const std::string& printerId, int pageNumber, int pageSize);
    PrinterNetworkResult<bool>                     deletePrintTasks(const std::string& printerId, const std::vector<std::string>& taskIds);
    PrinterNetworkResult<bool>                     sendRtmMessage(const std::string& printerId, const std::string& message);

    PrinterNetworkResult<std::vector<LicenseExpiredDevice>> getLicenseExpiredDevices();
    PrinterNetworkResult<bool>                              renewLicense(const std::string& serialNumber);
    PrinterNetworkResult<bool>                              refreshPrinterStatus(const std::string& printerId);
    PrinterNetworkResult<std::string>                       getPrinterStatusRaw(const std::string& printerId);
    void                                                    enqueueWanSyncRequest();

    PrinterNetworkResult<std::string>    upload(const PrinterNetworkParams& params);
    PrinterNetworkResult<UploadTaskInfo> getUploadTask(const std::string& taskId);
    PrinterNetworkResult<bool>           cancelUploadTask(const std::string& taskId);

    UserNetworkInfo                                       getUserInfo();
    void                                                  login(const UserNetworkInfo& userInfo);
    void                                                  logout();
    void                                                  reportTelemetryEvent(const std::string& eventName, const nlohmann::json& content, const std::string& pageName);
    PrinterNetworkResult<bool>                            checkUserNeedReLogin();
    UserNetworkInfo                                       refreshToken(const UserNetworkInfo& userInfo);
    PrinterNetworkResult<UserNetworkInfo>                 getRtcToken();
    PrinterNetworkResult<PrinterNetworkInfo>              bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool>                            unbindWANPrinter(const std::string& serialNumber);
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> getUserBoundPrinters();
    void checkUserAuthStatus(const UserNetworkInfo& requestUserInfo, const PrinterNetworkErrorCode& errorCode);

private:
    IPCClient();
    ~IPCClient();

    IPCResponse sendRequest(const std::string& method, const nlohmann::json& params);
    std::string generateRequestId();
    IPCResponse handlePendingRequestError(const std::string& id, PrinterNetworkErrorCode errorCode);

    void doReadLength();
    void doReadBody(uint32_t length);
    void handleMessage(const nlohmann::json& messageJson);
    void handleResponse(const nlohmann::json& responseJson);
    void handleEvent(const nlohmann::json& eventJson);
    void queueWrite(std::string message);
    void doWrite();
    bool connectToMaster();
    void disconnect(PrinterNetworkErrorCode errorCode = PrinterNetworkErrorCode::IPC_CONNECTION_CLOSED);
    void disconnectSync(PrinterNetworkErrorCode errorCode);
    void reconnectLoop();
    void cleanupResources();

    std::unique_ptr<boost::asio::io_context>                                                  mIoContext;
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> mWorkGuard;
    std::unique_ptr<boost::asio::strand<boost::asio::io_context::executor_type>>              mStrand;
    std::thread                                                                               mIoThread;
    std::thread::id                                                                           mIoThreadId;
    std::thread                                                                               mReconnectThread;
    std::atomic<bool>                                                                         mReconnectThreadRunning{false};
    std::unique_ptr<boost::asio::ip::tcp::socket>                                             mSocket;

    std::atomic<bool>     mConnected{false};
    std::atomic<uint32_t> mRequestIdCounter{0};

    std::mutex                                                        mConnectionMutex;
    std::mutex                                                        mPendingMutex;
    std::map<std::string, std::shared_ptr<std::promise<IPCResponse>>> mPendingRequests;

    std::deque<std::string> mWriteQueue;
};

} // namespace Slic3r
