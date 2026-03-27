#include "IPCClient.hpp"
#include "IPCCommon.hpp"
#include "slic3r/Utils/IPCMessage.hpp"
#include "libslic3r/PrinterNetworkResult.hpp"
#include "slic3r/Utils/JsonUtils.hpp"  
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "PrinterNetworkEvent.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <chrono>


#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Slic3r {

IPCClient::IPCClient() {}

IPCClient::~IPCClient() { stop(); }

void IPCClient::start()
{
    BOOST_LOG_TRIVIAL(info) << "IPCClient::start";
    connectToMaster();
    if (!mReconnectThreadRunning.exchange(true)) {
        mReconnectThread = std::thread([this]() { reconnectLoop(); });
        BOOST_LOG_TRIVIAL(info) << "IPCClient: reconnect thread started";
    }
}

void IPCClient::stop()
{
    BOOST_LOG_TRIVIAL(info) << "IPCClient::stop";
    if (mReconnectThreadRunning.exchange(false)) {
        if (mReconnectThread.joinable()) {
            mReconnectThread.join();
        }
    }
    disconnect();
}

bool IPCClient::connectToMaster()
{
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    if (mConnected.load()) {
        BOOST_LOG_TRIVIAL(info) << "IPCClient::connectToMaster: already connected";
        return true;
    }

    unsigned short port = 0;
    std::string portFile = getIPCPortFilePath();
    boost::nowide::ifstream file(portFile);
    if (file.is_open()) {
        file >> port;
        file.close();
    } else {
        BOOST_LOG_TRIVIAL(warning) << "IPCClient: port file not found: " << portFile << ", server may not be running";
        return false;
    }

    if (port == 0) {
        BOOST_LOG_TRIVIAL(error) << "IPCClient: invalid port read from file: " << portFile;
        return false;
    }

    mIoContext = std::make_unique<boost::asio::io_context>();

    try {
        mSocket = std::make_unique<boost::asio::ip::tcp::socket>(*mIoContext);
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address_v4::loopback(), port);
        mSocket->connect(endpoint);
        BOOST_LOG_TRIVIAL(info) << "IPCClient: connected to master on port " << port;

        if (!mSocket->is_open()) {
            cleanupResources();
            return false;
        }

        mStrand = std::make_unique<boost::asio::strand<boost::asio::io_context::executor_type>>(mIoContext->get_executor());
        mWorkGuard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(mIoContext->get_executor());

        mIoThread = std::thread([this]() {
            mIoThreadId = std::this_thread::get_id();
            try {
                mIoContext->run();
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "IPCClient: I/O thread exception: " << e.what();
            }
        });

        mConnected.store(true);       
        boost::asio::post(*mStrand, [this]() { doReadLength(); });
    } catch (const boost::system::system_error& e) {
        BOOST_LOG_TRIVIAL(error) << "IPCClient: connect failed: " << e.what();
        cleanupResources();
        mConnected.store(false);
        return false;
    }

    return true;
}

void IPCClient::disconnect(PrinterNetworkErrorCode errorCode)
{
    if (std::this_thread::get_id() == mIoThreadId) {
        disconnectSync(errorCode);
        return;
    }

    std::lock_guard<std::mutex> lock(mConnectionMutex);

    if (!mConnected.load()) {
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "IPCClient::disconnect (code=" << static_cast<int>(errorCode) << ")";
    mConnected.store(false);

    {
        // 1. Clear the pending requests
        std::lock_guard<std::mutex> lockPending(mPendingMutex);
        for (auto& [id, promise] : mPendingRequests) {
            try {
                IPCResponse errorResponse;
                errorResponse.id   = id;
                errorResponse.code = static_cast<int>(errorCode);
                promise->set_value(errorResponse);
            } catch (...) {
            }
        }
        mPendingRequests.clear();
    }

    // 2. Clear the write queue safely
    if (mStrand && mIoContext) {
        std::promise<void> clearPromise;
        auto clearFuture = clearPromise.get_future();
        boost::asio::post(*mStrand, [this, &clearPromise]() {
            mWriteQueue.clear();
            clearPromise.set_value();
        });
        clearFuture.wait();
    } else {
        mWriteQueue.clear();
    }

    cleanupResources();
}

void IPCClient::cleanupResources()
{
    boost::system::error_code ec;
    if (mSocket) {
        mSocket->close(ec);
        mSocket.reset();
    }

    if (mWorkGuard) {
        mWorkGuard.reset();
    }

    if (mIoContext) {
        mIoContext->stop();
    }

    if (mIoThread.joinable()) {
        mIoThread.join();
    }

    if (mStrand) {
        mStrand.reset();
    }

    if (mIoContext) {
        mIoContext.reset();
    }
}

void IPCClient::disconnectSync(PrinterNetworkErrorCode errorCode)
{
    std::thread([errorCode]() {
        IPCClient::getInstance()->disconnect(errorCode);      
    }).detach();
}

void IPCClient::doReadLength()
{
    if (!mConnected.load()) {
        return;
    }

    if (!mSocket || !mStrand) {
        return;
    }

    auto buffer = std::make_shared<std::array<char, 4>>();

    boost::asio::async_read(*mSocket, boost::asio::buffer(*buffer),
                            boost::asio::bind_executor(*mStrand, [this, buffer](const boost::system::error_code& ec, std::size_t) {
                                if (ec) {
                                    if (ec != boost::asio::error::operation_aborted) {
                                        BOOST_LOG_TRIVIAL(warning) << "IPCClient: read length error detected: " << ec.message();
                                        disconnectSync(PrinterNetworkErrorCode::IPC_IO_ERROR);
                                    }
                                    return;
                                }

                                uint32_t msgLength = (static_cast<uint8_t>((*buffer)[0]) << 24) |
                                                     (static_cast<uint8_t>((*buffer)[1]) << 16) |
                                                     (static_cast<uint8_t>((*buffer)[2]) << 8) |
                                                     (static_cast<uint8_t>((*buffer)[3]));

                                if (msgLength == 0 || msgLength > IPC_MAX_MESSAGE_SIZE) {
                                    BOOST_LOG_TRIVIAL(error) << "IPCClient: invalid message length: " << msgLength;
                                    disconnectSync(PrinterNetworkErrorCode::IPC_INVALID_MESSAGE);
                                    return;
                                }

                                doReadBody(msgLength);
                            }));
}

void IPCClient::doReadBody(uint32_t length)
{
    if (!mConnected.load()) {
        return;
    }

    if (!mSocket || !mStrand) {
        return;
    }

    auto buffer = std::make_shared<std::vector<char>>(length);

    boost::asio::async_read(*mSocket, boost::asio::buffer(*buffer),
                            boost::asio::bind_executor(*mStrand, [this, buffer](const boost::system::error_code& ec, std::size_t) {
                                if (ec) {
                                    if (ec != boost::asio::error::operation_aborted) {
                                        BOOST_LOG_TRIVIAL(warning) << "IPCClient: read body error detected: " << ec.message();
                                        disconnectSync(PrinterNetworkErrorCode::IPC_IO_ERROR);
                                    }
                                    return;
                                }

                                std::string messageData(buffer->begin(), buffer->end());

                                try {
                                    nlohmann::json messageJson = parseIPCMessage(messageData);
                                    handleMessage(messageJson);
                                } catch (const std::exception& e) {
                                    BOOST_LOG_TRIVIAL(error) << "IPCClient: parse message error: " << e.what();
                                }

                                doReadLength();
                            }));
}

void IPCClient::handleMessage(const nlohmann::json& messageJson)
{
    if (!messageJson.is_object()) {
        BOOST_LOG_TRIVIAL(warning) << "IPCClient: message is not a valid JSON object";
        return;
    }

    std::string type = messageJson.value("type", "");
    
    if (type == "response") {
        handleResponse(messageJson);
    } else if (type == "event") {
        handleEvent(messageJson);
    } else {
        BOOST_LOG_TRIVIAL(warning) << "IPCClient: unknown message type: " << type;
    }
}

void IPCClient::handleResponse(const nlohmann::json& responseJson)
{
    std::string id = responseJson.value("id", "");

    std::lock_guard<std::mutex> lock(mPendingMutex);
    auto                        it = mPendingRequests.find(id);
    if (it != mPendingRequests.end()) {
        IPCResponse response;
        response.id      = responseJson.value("id", "");
        response.method  = responseJson.value("method", "");
        response.code    = responseJson.value("code", 0);
        response.message = responseJson.value("message", "");
        response.data    = responseJson.value("data", nlohmann::json::object());

        try {
            it->second->set_value(response);
        } catch (...) {
        }

        mPendingRequests.erase(it);
    }
}

void IPCClient::handleEvent(const nlohmann::json& eventJson)
{
    if (!eventJson.is_object()) {
        BOOST_LOG_TRIVIAL(warning) << "IPCClient: event message is not a valid JSON object";
        return;
    }

    std::string method = eventJson.value("method", "");
    if (method.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "IPCClient: event message missing or invalid method field";
        return;
    }

    std::string id   = eventJson.value("id", "");
    nlohmann::json data = eventJson.value("data", nlohmann::json::object());

    BOOST_LOG_TRIVIAL(info) << "IPCClient: received event '" << method << "' (id: " << id << ")";

    if (method == "user.userInfoChanged") {
        if (wxGetApp().mainframe && wxGetApp().mainframe->is_loaded()) {
            auto evt = new wxCommandEvent(EVT_USER_INFO_UPDATED);
            wxQueueEvent(wxGetApp().mainframe, evt);
            BOOST_LOG_TRIVIAL(info) << "IPCClient: sent EVT_USER_INFO_UPDATED event to mainframe";
        }
    }
    if (method == "printer.connectStatusChanged") {
        std::string printerId = JsonUtils::safeGetString(data, "printerId", "");
        PrinterConnectStatus status = static_cast<PrinterConnectStatus>(JsonUtils::safeGetInt(data, "status", PRINTER_CONNECT_STATUS_DISCONNECTED));
        PrinterNetworkEvent::getInstance()->connectStatusChanged.emit(PrinterConnectStatusEvent(printerId, status));
    }
    if (method == "printer.eventRawChanged") {
        std::string printerId = JsonUtils::safeGetString(data, "printerId", "");
        std::string event = JsonUtils::safeGetString(data, "event", "");
        PrinterNetworkEvent::getInstance()->eventRawChanged.emit(PrinterEventRawEvent(printerId, event));
    }
    if (method == "user.rtcTokenChanged") {
        UserNetworkInfo userInfo;
        userInfo.userId             = JsonUtils::safeGetString(data, "userId", "");
        userInfo.rtcToken           = JsonUtils::safeGetString(data, "rtcToken", "");
        userInfo.rtcTokenExpireTime = JsonUtils::safeGetInt64(data, "rtcTokenExpireTime", 0);
        UserNetworkEvent::getInstance()->rtcTokenChanged.emit(UserRtcTokenEvent(userInfo));
    }
    if (method == "user.rtmMessageChanged") {
        std::string printerId = JsonUtils::safeGetString(data, "printerId", "");
        std::string message = JsonUtils::safeGetString(data, "message", "");
        UserNetworkEvent::getInstance()->rtmMessageChanged.emit(UserRtmMessageEvent(printerId, message));
    }

}

void IPCClient::queueWrite(std::string message)
{
    if (!mConnected.load() || !mStrand) {
        return;
    }

    boost::asio::post(*mStrand, [this, msg = std::move(message)]() mutable {
        if (!mConnected.load()) {
            return;
        }

        bool writeInProgress = !mWriteQueue.empty();
        mWriteQueue.push_back(std::move(msg));
        if (!writeInProgress) {
            doWrite();
        }
    });
}

void IPCClient::doWrite()
{
    if (mWriteQueue.empty() || !mConnected.load()) {
        return;
    }

    if (!mSocket || !mStrand) {
        disconnectSync(PrinterNetworkErrorCode::IPC_SEND_FAILED);
        return;
    }

    const std::string& message = mWriteQueue.front();
    uint32_t           length  = static_cast<uint32_t>(message.size());

    auto lengthBuffer = std::make_shared<std::array<char, 4>>();
    (*lengthBuffer)[0] = static_cast<char>((length >> 24) & 0xFF);
    (*lengthBuffer)[1] = static_cast<char>((length >> 16) & 0xFF);
    (*lengthBuffer)[2] = static_cast<char>((length >> 8) & 0xFF);
    (*lengthBuffer)[3] = static_cast<char>(length & 0xFF);

    auto msgBuffer = std::make_shared<std::string>(message);

    std::array<boost::asio::const_buffer, 2> buffers = {
        boost::asio::buffer(*lengthBuffer),
        boost::asio::buffer(*msgBuffer)
    };

    boost::asio::async_write(*mSocket, buffers,
                             boost::asio::bind_executor(*mStrand, [this, lengthBuffer, msgBuffer](const boost::system::error_code& ec,
                                                                                                   std::size_t) {
                                 mWriteQueue.pop_front();

                                 if (ec) {
                                     BOOST_LOG_TRIVIAL(warning) << "IPCClient: write error detected: " << ec.message();
                                     disconnectSync(PrinterNetworkErrorCode::IPC_SEND_FAILED);
                                 } else if (!mWriteQueue.empty()) {
                                     doWrite();
                                 }
                             }));
}

std::string IPCClient::generateRequestId()
{
#ifdef _WIN32
    int pid = static_cast<int>(_getpid());
#else
    int pid = static_cast<int>(getpid());
#endif
    return "ipc-" + std::to_string(pid) + "-" + std::to_string(++mRequestIdCounter);
}

IPCResponse IPCClient::handlePendingRequestError(const std::string& id, PrinterNetworkErrorCode errorCode)
{
    std::lock_guard<std::mutex> lock(mPendingMutex);
    auto                        it = mPendingRequests.find(id);
    if (it != mPendingRequests.end()) {
        try {
            IPCResponse errorResponse;
            errorResponse.id   = id;
            errorResponse.code = static_cast<int>(errorCode);
            it->second->set_value(errorResponse);
        } catch (...) {
        }
        mPendingRequests.erase(it);
    }
    IPCResponse response;
    response.code = static_cast<int>(errorCode);
    return response;
}

IPCResponse IPCClient::sendRequest(const std::string& method, const nlohmann::json& params)
{
    if (std::this_thread::get_id() == mIoThreadId) {
        BOOST_LOG_TRIVIAL(error) << "IPCClient: sendRequest called from I/O thread (API misuse), method: " << method;
        IPCResponse errorResponse;
        errorResponse.code = static_cast<int>(PrinterNetworkErrorCode::IPC_DEADLOCK_PREVENTED);
        return errorResponse;
    }

    std::string id = generateRequestId();

    nlohmann::json requestJson;
    requestJson["id"]     = id;
    requestJson["type"]   = "request";
    requestJson["method"] = method;
    requestJson["params"] = params;

    auto promise = std::make_shared<std::promise<IPCResponse>>();
    auto future  = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(mConnectionMutex);

        if (!mConnected.load()) {
            IPCResponse errorResponse;
            errorResponse.code = static_cast<int>(PrinterNetworkErrorCode::IPC_NOT_CONNECTED);
            return errorResponse;
        }

        {
            std::lock_guard<std::mutex> lockPending(mPendingMutex);
            if (mPendingRequests.size() >= IPC_MAX_PENDING_WRITES) {
                IPCResponse errorResponse;
                errorResponse.code = static_cast<int>(PrinterNetworkErrorCode::IPC_TOO_MANY_PENDING);
                return errorResponse;
            }
            mPendingRequests[id] = promise;
        }

        try {
            std::string requestStr = serializeIPCMessage(requestJson);
            queueWrite(std::move(requestStr));
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "IPCClient: serializeIPCMessage failed: " << e.what();
            handlePendingRequestError(id, PrinterNetworkErrorCode::IPC_PROTOCOL_ERROR);
            IPCResponse errorResponse;
            errorResponse.code = static_cast<int>(PrinterNetworkErrorCode::IPC_PROTOCOL_ERROR);
            return errorResponse;
        }
    }

    auto status = future.wait_for(std::chrono::seconds(IPC_REQUEST_TIMEOUT_SECONDS));
    if (status == std::future_status::timeout) {
        return handlePendingRequestError(id, PrinterNetworkErrorCode::OPERATION_TIMEOUT);
    }

    return future.get();
}

PrinterNetworkInfo IPCClient::getPrinterNetworkInfo(const std::string& printerId)
{
    nlohmann::json params;
    params["printerId"]  = printerId;
    IPCResponse response = sendRequest("printer.getPrinterNetworkInfo", params);
    if (response.code == 0 && !response.data.is_null()) {
        return convertJsonToPrinterNetworkInfo(response.data);
    }
    return PrinterNetworkInfo();
}

std::vector<PrinterNetworkInfo> IPCClient::getPrinterList()
{
    IPCResponse response = sendRequest("printer.getPrinterList", nlohmann::json::object());
    if (response.code == 0 && response.data.is_array()) {
        std::vector<PrinterNetworkInfo> printers;
        for (const auto& printerJson : response.data) {
            printers.push_back(convertJsonToPrinterNetworkInfo(printerJson));
        }
        return printers;
    }
    return std::vector<PrinterNetworkInfo>();
}

PrinterNetworkInfo IPCClient::getSelectedPrinter(const std::string& printerModel, const std::string& printerId)
{
    nlohmann::json params;
    params["printerModel"] = printerModel;
    params["printerId"]    = printerId;
    IPCResponse response   = sendRequest("printer.getSelectedPrinter", params);
    if (response.code == 0 && !response.data.is_null()) {
        return convertJsonToPrinterNetworkInfo(response.data);
    }
    return PrinterNetworkInfo();
}

PrinterNetworkResult<std::vector<PrinterNetworkInfo>> IPCClient::discoverPrinter()
{
    IPCResponse                     response = sendRequest("printer.discoverPrinter", nlohmann::json::object());
    std::vector<PrinterNetworkInfo> printers;
    if (response.data.is_array()) {
        for (const auto& printerJson : response.data) {
            printers.push_back(convertJsonToPrinterNetworkInfo(printerJson));
        }
    }
    return PrinterNetworkResult<std::vector<PrinterNetworkInfo>>(static_cast<PrinterNetworkErrorCode>(response.code), printers,
                                                                 response.message);
}

PrinterNetworkResult<bool> IPCClient::cancelBindPrinter(const PrinterNetworkInfo& printerNetworkInfo)
{
    nlohmann::json params   = convertPrinterNetworkInfoToJson(printerNetworkInfo);
    IPCResponse    response = sendRequest("printer.cancelBindPrinter", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<bool> IPCClient::addPrinter(PrinterNetworkInfo& printerNetworkInfo)
{
    nlohmann::json params   = convertPrinterNetworkInfoToJson(printerNetworkInfo);
    IPCResponse    response = sendRequest("printer.addPrinter", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<bool> IPCClient::deletePrinter(const std::string& printerId)
{
    nlohmann::json params;
    params["printerId"]  = printerId;
    IPCResponse response = sendRequest("printer.deletePrinter", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<bool> IPCClient::updatePrinterName(const std::string& printerId, const std::string& name)
{
    nlohmann::json params;
    params["printerId"]  = printerId;
    params["name"]       = name;
    IPCResponse response = sendRequest("printer.updatePrinterName", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<bool> IPCClient::updatePrinterHost(const std::string& printerId, const std::string& host)
{
    nlohmann::json params;
    params["printerId"]  = printerId;
    params["host"]       = host;
    IPCResponse response = sendRequest("printer.updatePrinterHost", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<bool> IPCClient::updatePhysicalPrinter(const std::string& printerId, const PrinterNetworkInfo& printerInfo)
{
    nlohmann::json params;
    params["printerId"]   = printerId;
    params["printerInfo"] = convertPrinterNetworkInfoToJson(printerInfo);
    IPCResponse response  = sendRequest("printer.updatePhysicalPrinter", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<PrinterMmsGroup> IPCClient::getPrinterMmsInfo(const std::string& printerId)
{
    nlohmann::json params;
    params["printerId"]      = printerId;
    IPCResponse     response = sendRequest("printer.getPrinterMmsInfo", params);
    PrinterMmsGroup mmsGroup;
    if (!response.data.is_null()) {
        mmsGroup = convertJsonToPrinterMmsGroup(response.data);
    }
    return PrinterNetworkResult<PrinterMmsGroup>(static_cast<PrinterNetworkErrorCode>(response.code), mmsGroup, response.message);
}

PrinterNetworkResult<PrinterPrintFileResponse> IPCClient::getFileList(const std::string& printerId, int pageNumber, int pageSize)
{
    nlohmann::json params;
    params["printerId"]               = printerId;
    params["pageNumber"]              = pageNumber;
    params["pageSize"]                = pageSize;
    IPCResponse              response = sendRequest("printer.getFileList", params);
    PrinterPrintFileResponse fileResponse;
    if (!response.data.is_null()) {
        fileResponse = convertJsonToPrinterPrintFileResponse(response.data);
    }
    return PrinterNetworkResult<PrinterPrintFileResponse>(static_cast<PrinterNetworkErrorCode>(response.code), fileResponse,
                                                          response.message);
}

PrinterNetworkResult<PrinterPrintTaskResponse> IPCClient::getPrintTaskList(const std::string& printerId, int pageNumber, int pageSize)
{
    nlohmann::json params;
    params["printerId"]               = printerId;
    params["pageNumber"]              = pageNumber;
    params["pageSize"]                = pageSize;
    IPCResponse              response = sendRequest("printer.getPrintTaskList", params);
    PrinterPrintTaskResponse taskResponse;
    if (!response.data.is_null()) {
        taskResponse = convertJsonToPrinterPrintTaskResponse(response.data);
    }
    return PrinterNetworkResult<PrinterPrintTaskResponse>(static_cast<PrinterNetworkErrorCode>(response.code), taskResponse,
                                                          response.message);
}

PrinterNetworkResult<bool> IPCClient::deletePrintTasks(const std::string& printerId, const std::vector<std::string>& taskIds)
{
    nlohmann::json params;
    params["printerId"]  = printerId;
    params["taskIds"]    = taskIds;
    IPCResponse response = sendRequest("printer.deletePrintTasks", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<bool> IPCClient::sendRtmMessage(const std::string& printerId, const std::string& message)
{
    nlohmann::json params;
    params["printerId"]  = printerId;
    params["message"]    = message;
    IPCResponse response = sendRequest("printer.sendRtmMessage", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<PrinterPrintFileResponse> IPCClient::getFileDetail(const std::string& printerId, const std::string& fileName)
{
    nlohmann::json params;
    params["printerId"]               = printerId;
    params["fileName"]                = fileName;
    IPCResponse              response = sendRequest("printer.getFileDetail", params);
    PrinterPrintFileResponse fileResponse;
    if (!response.data.is_null()) {
        fileResponse = convertJsonToPrinterPrintFileResponse(response.data);
    }
    return PrinterNetworkResult<PrinterPrintFileResponse>(static_cast<PrinterNetworkErrorCode>(response.code), fileResponse,
                                                          response.message);
}

PrinterNetworkResult<std::vector<LicenseExpiredDevice>> IPCClient::getLicenseExpiredDevices()
{
    IPCResponse response = sendRequest("printer.getLicenseExpiredDevices", nlohmann::json::object());

    std::vector<LicenseExpiredDevice> devices;
    if (response.code == 0 && response.data.is_array()) {
        for (const auto& item : response.data) {
            LicenseExpiredDevice dev;
            if (item.is_object()) {
                dev.serialNumber = item.value("serialNumber", "");
                dev.status       = item.value("status", 0);
            }
            devices.push_back(dev);
        }
    }

    return PrinterNetworkResult<std::vector<LicenseExpiredDevice>>(
        static_cast<PrinterNetworkErrorCode>(response.code), devices, response.message);
}

PrinterNetworkResult<bool> IPCClient::renewLicense(const std::string& serialNumber)
{
    nlohmann::json params;
    params["serialNumber"] = serialNumber;
    IPCResponse response   = sendRequest("printer.renewLicense", params);
    return PrinterNetworkResult<bool>(
        static_cast<PrinterNetworkErrorCode>(response.code),
        response.data.is_boolean() ? response.data.get<bool>() : false,
        response.message);
}

PrinterNetworkResult<bool> IPCClient::refreshPrinterStatus(const std::string& printerId)
{
    nlohmann::json params;
    params["printerId"] = printerId;
    IPCResponse response =
        sendRequest("printer.refreshPrinterStatus", params);
    return PrinterNetworkResult<bool>(
        static_cast<PrinterNetworkErrorCode>(response.code),
        response.data.is_boolean() ? response.data.get<bool>() : false,
        response.message);
}

PrinterNetworkResult<std::string> IPCClient::getPrinterStatusRaw(const std::string& printerId)
{
    nlohmann::json params;
    params["printerId"] = printerId;
    IPCResponse response =
        sendRequest("printer.getPrinterStatusRaw", params);

    std::string status;
    if (response.code == 0 && response.data.is_string()) {
        status = response.data.get<std::string>();
    }

    return PrinterNetworkResult<std::string>(
        static_cast<PrinterNetworkErrorCode>(response.code),
        status,
        response.message);
}

void IPCClient::enqueueWanSyncRequest()
{
    sendRequest("printer.enqueueWanSyncRequest", nlohmann::json::object());
}

PrinterNetworkResult<std::string> IPCClient::upload(const PrinterNetworkParams& params)
{
    nlohmann::json jsonParams = convertPrinterNetworkParamsToJson(params);

    IPCResponse response = sendRequest("printer.upload", jsonParams);

    PrinterNetworkErrorCode code = static_cast<PrinterNetworkErrorCode>(response.code);
    if (code == PrinterNetworkErrorCode::SUCCESS && response.data.contains("taskId")) {
        return PrinterNetworkResult<std::string>(code, response.data["taskId"].get<std::string>(), response.message);
    }
    return PrinterNetworkResult<std::string>(code, "", response.message);
}

PrinterNetworkResult<UploadTaskInfo> IPCClient::getUploadTask(const std::string& taskId)
{
    nlohmann::json params;
    params["taskId"]     = taskId;
    IPCResponse response = sendRequest("printer.getUploadTask", params);

    PrinterNetworkErrorCode code = static_cast<PrinterNetworkErrorCode>(response.code);
    UploadTaskInfo          task = convertJsonToUploadTaskInfo(response.data);
    return PrinterNetworkResult<UploadTaskInfo>(code, task, response.message);
}

PrinterNetworkResult<bool> IPCClient::cancelUploadTask(const std::string& taskId)
{
    nlohmann::json params;
    params["taskId"]     = taskId;
    IPCResponse response = sendRequest("printer.cancelUploadTask", params);

    PrinterNetworkErrorCode code    = static_cast<PrinterNetworkErrorCode>(response.code);
    bool                    success = response.data.is_boolean() ? response.data.get<bool>() : false;
    return PrinterNetworkResult<bool>(code, success, response.message);
}

UserNetworkInfo IPCClient::getUserInfo()
{
    IPCResponse response = sendRequest("user.getUserInfo", nlohmann::json::object());
    if (response.code == 0 && !response.data.is_null()) {
        return convertJsonToUserNetworkInfo(response.data);
    }
    return UserNetworkInfo();
}

void IPCClient::login(const UserNetworkInfo& userInfo)
{
    nlohmann::json params = convertUserNetworkInfoToJson(userInfo);
    sendRequest("user.login", params);
}

void IPCClient::logout() { sendRequest("user.logout", nlohmann::json::object()); }

PrinterNetworkResult<bool> IPCClient::checkUserNeedReLogin()
{
    IPCResponse response = sendRequest("user.checkUserNeedReLogin", nlohmann::json::object());
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

UserNetworkInfo IPCClient::refreshToken(const UserNetworkInfo& userInfo)
{
    nlohmann::json params   = convertUserNetworkInfoToJson(userInfo);
    IPCResponse    response = sendRequest("user.refreshToken", params);
    if (response.code == 0 && !response.data.is_null()) {
        return convertJsonToUserNetworkInfo(response.data);
    }
    return UserNetworkInfo();
}

PrinterNetworkResult<UserNetworkInfo> IPCClient::getRtcToken()
{
    IPCResponse     response = sendRequest("user.getRtcToken", nlohmann::json::object());
    UserNetworkInfo userInfo;
    if (!response.data.is_null()) {
        userInfo = convertJsonToUserNetworkInfo(response.data);
    }
    return PrinterNetworkResult<UserNetworkInfo>(static_cast<PrinterNetworkErrorCode>(response.code), userInfo, response.message);
}

PrinterNetworkResult<PrinterNetworkInfo> IPCClient::bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo)
{
    nlohmann::json     params   = convertPrinterNetworkInfoToJson(printerNetworkInfo);
    IPCResponse        response = sendRequest("user.bindWANPrinter", params);
    PrinterNetworkInfo printerInfo;
    if (!response.data.is_null()) {
        printerInfo = convertJsonToPrinterNetworkInfo(response.data);
    }
    return PrinterNetworkResult<PrinterNetworkInfo>(static_cast<PrinterNetworkErrorCode>(response.code), printerInfo, response.message);
}

PrinterNetworkResult<bool> IPCClient::unbindWANPrinter(const std::string& serialNumber)
{
    nlohmann::json params;
    params["serialNumber"] = serialNumber;
    IPCResponse response   = sendRequest("user.unbindWANPrinter", params);
    return PrinterNetworkResult<bool>(static_cast<PrinterNetworkErrorCode>(response.code),
                                      response.data.is_boolean() ? response.data.get<bool>() : false, response.message);
}

PrinterNetworkResult<std::vector<PrinterNetworkInfo>> IPCClient::getUserBoundPrinters()
{
    IPCResponse                     response = sendRequest("user.getUserBoundPrinters", nlohmann::json::object());
    std::vector<PrinterNetworkInfo> printers;
    if (response.data.is_array()) {
        for (const auto& printerJson : response.data) {
            printers.push_back(convertJsonToPrinterNetworkInfo(printerJson));
        }
    }
    return PrinterNetworkResult<std::vector<PrinterNetworkInfo>>(static_cast<PrinterNetworkErrorCode>(response.code), printers,
                                                                 response.message);
}

void IPCClient::checkUserAuthStatus(const UserNetworkInfo& requestUserInfo, const PrinterNetworkErrorCode& errorCode)
{
    nlohmann::json params;
    params["requestUserInfo"] = convertUserNetworkInfoToJson(requestUserInfo);
    params["errorCode"]       = static_cast<int>(errorCode);
    sendRequest("user.checkUserAuthStatus", params);
}

void IPCClient::reconnectLoop()
{
    while (mReconnectThreadRunning.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(IPC_RECONNECT_INTERVAL_SECONDS));

        if (!mReconnectThreadRunning.load()) {
            break;
        }

        if (!mConnected.load()) {
            if (connectToMaster()) {
                BOOST_LOG_TRIVIAL(info) << "IPCClient: auto-reconnect successful";
            }
        }
    }
}

} // namespace Slic3r
