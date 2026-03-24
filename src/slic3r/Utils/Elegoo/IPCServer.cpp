#include "IPCServer.hpp"
#include "IPCCommon.hpp"
#include "PrinterManager.hpp"
#include "PrinterUploadManager.hpp"
#include "UserNetworkManager.hpp"
#include "PrinterNetworkEvent.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "slic3r/Utils/IPCMessage.hpp"
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <deque>
#include <memory>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Slic3r {

class IPCServer::Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::io_context& ioCtx, boost::asio::thread_pool& businessPool, IPCServer* server,
            std::shared_ptr<boost::asio::ip::tcp::socket> socket)
        : mStrand(boost::asio::make_strand(ioCtx)), mBusinessPool(businessPool), mServer(server), mSocket(socket)
    {}

    void start()
    {
        {
            std::lock_guard<std::mutex> lock(mServer->mSessionsMutex);
            mServer->mSessions.insert(shared_from_this());
        }
        boost::asio::post(mStrand, [self = shared_from_this()]() { self->doReadLength(); });
    }

    void close()
    {
        auto self = shared_from_this();
        boost::asio::post(mStrand, [self]() {
            if (self->mClosed.exchange(true))
                return;

            boost::system::error_code ec;
            self->mSocket->close(ec);
            self->mWriteQueue.clear();

            {
                std::lock_guard<std::mutex> lock(self->mServer->mSessionsMutex);
                self->mServer->mSessions.erase(self);
            }
        });
    }

    void closeSync()
    {
        if (mClosed.exchange(true))
            return;
        
        boost::system::error_code ec;
        mSocket->close(ec);
        mWriteQueue.clear();
    }

    void sendEvent(const IPCEvent& event)
    {
        if (mClosed.load())
            return;
        std::string id = event.id;
        if (id.empty()) {
            static std::atomic<uint32_t> counter{0};
#ifdef _WIN32
            int pid = static_cast<int>(_getpid());
#else
            int pid = static_cast<int>(getpid());
#endif
            id = "ipc-" + std::to_string(pid) + "-" + std::to_string(++counter);
        }
        nlohmann::json eventJson;
        eventJson["type"]   = "event";
        eventJson["method"] = event.method;
        eventJson["data"]   = event.data;
        eventJson["id"]     = id;
        std::string eventStr = serializeIPCMessage(eventJson);
        BOOST_LOG_TRIVIAL(info) << "IPCServer: sending event '" << event.method << "' (id: " << id << ")";
        queueWrite(std::move(eventStr));
    }

private:

    void doReadLength()
    {
        if (mClosed.load())
            return;

        auto self   = shared_from_this();
        auto buffer = std::make_shared<std::array<char, 4>>();

        boost::asio::async_read(*mSocket, boost::asio::buffer(*buffer),
                                boost::asio::bind_executor(mStrand, [this, self, buffer](const boost::system::error_code& ec, std::size_t) {
                                    if (mClosed.load())
                                        return;

                                    if (ec) {
                                        if (ec != boost::asio::error::operation_aborted) {
                                            close();
                                        }
                                        return;
                                    }

                                    uint32_t msgLength = (static_cast<uint8_t>((*buffer)[0]) << 24) |
                                                         (static_cast<uint8_t>((*buffer)[1]) << 16) |
                                                         (static_cast<uint8_t>((*buffer)[2]) << 8) |
                                                         (static_cast<uint8_t>((*buffer)[3]));

                                    if (msgLength == 0 || msgLength > IPC_MAX_MESSAGE_SIZE) {
                                        BOOST_LOG_TRIVIAL(error) << "invalid message length: " << msgLength;
                                        close();
                                        return;
                                    }

                                    doReadBody(msgLength);
                                }));
    }

    void doReadBody(uint32_t length)
    {
        if (mClosed.load())
            return;

        auto self   = shared_from_this();
        auto buffer = std::make_shared<std::vector<char>>(length);

        boost::asio::async_read(*mSocket, boost::asio::buffer(*buffer),
                                boost::asio::bind_executor(mStrand, [this, self, buffer](const boost::system::error_code& ec, std::size_t) {
                                    if (mClosed.load())
                                        return;

                                    if (ec) {
                                        if (ec != boost::asio::error::operation_aborted) {
                                            close();
                                        }
                                        return;
                                    }

                                    std::string requestData(buffer->begin(), buffer->end());
                                    boost::asio::post(mBusinessPool, [this, self, requestData]() { processRequest(requestData); });
                                    doReadLength();
                                }));
    }

    void processRequest(const std::string& requestData)
    {
        if (mClosed.load()) {
            return;
        }

        nlohmann::json reqJson = parseIPCMessage(requestData);
        if (reqJson.is_null() || reqJson.empty()) {
            BOOST_LOG_TRIVIAL(error) << "processRequest: invalid or empty JSON message, closing session";
            close();
            return;
        }

        if (!reqJson.contains("method") || !reqJson["method"].is_string()) {
            BOOST_LOG_TRIVIAL(error) << "processRequest: invalid method field, closing session";
            close();
            return;
        }

        IPCRequest request;
        request.id     = reqJson.value("id", "");
        request.method = reqJson.value("method", "");
        request.params = reqJson.value("params", nlohmann::json::object());

        IPCResponse response = mServer->handleRequest(request);

        if (mClosed.load()) {
            return;
        }

        nlohmann::json responseJson;
        responseJson["id"]      = response.id;
        responseJson["method"]  = response.method;
        responseJson["type"]    = "response";
        responseJson["code"]    = response.code;
        responseJson["message"] = response.message;
        responseJson["data"]    = response.data;

        std::string responseStr = serializeIPCMessage(responseJson);
        queueWrite(std::move(responseStr));
    }

    void queueWrite(std::string message)
    {
        if (mClosed.load())
            return;

        auto self = shared_from_this();
        boost::asio::post(mStrand, [this, self, msg = std::move(message)]() mutable {
            if (mClosed.load())
                return;

            if (mWriteQueue.size() >= IPC_MAX_PENDING_WRITES) {
                BOOST_LOG_TRIVIAL(error) << "Write queue overflow, closing session";
                close();
                return;
            }

            bool writeInProgress = !mWriteQueue.empty();
            mWriteQueue.push_back(std::move(msg));
            if (!writeInProgress) {
                doWrite();
            }
        });
    }

    void doWrite()
    {
        if (mWriteQueue.empty() || mClosed.load())
            return;

        auto               self    = shared_from_this();
        const std::string& message = mWriteQueue.front();

        uint32_t length       = static_cast<uint32_t>(message.size());
        auto     lengthBuffer = std::make_shared<std::array<char, 4>>();
        (*lengthBuffer)[0]    = static_cast<char>((length >> 24) & 0xFF);
        (*lengthBuffer)[1]    = static_cast<char>((length >> 16) & 0xFF);
        (*lengthBuffer)[2]    = static_cast<char>((length >> 8) & 0xFF);
        (*lengthBuffer)[3]    = static_cast<char>(length & 0xFF);

        auto msgBuffer = std::make_shared<std::string>(message);

        std::array<boost::asio::const_buffer, 2> buffers = {
            boost::asio::buffer(*lengthBuffer),
            boost::asio::buffer(*msgBuffer)
        };

        boost::asio::async_write(*mSocket, buffers,
                                 boost::asio::bind_executor(mStrand, [this, self, lengthBuffer, msgBuffer](const boost::system::error_code& ec,
                                                                                                           std::size_t) {
                                     mWriteQueue.pop_front();

                                     if (ec) {
                                         close();
                                     } else if (!mWriteQueue.empty()) {
                                         doWrite();
                                     }
                                 }));
    }

    boost::asio::strand<boost::asio::io_context::executor_type>  mStrand;
    boost::asio::thread_pool&                                    mBusinessPool;
    IPCServer*                                                   mServer;
    std::shared_ptr<boost::asio::ip::tcp::socket>                mSocket;
    std::deque<std::string>                                      mWriteQueue;
    std::atomic<bool>                                            mClosed{false};
};

IPCServer::IPCServer() {}

IPCServer::~IPCServer() { stop(); }

void IPCServer::start()
{
    std::lock_guard<std::mutex> lock(mServerMutex);
    
    if (mRunning.load()) {
        return;
    }

    try {
        // All resources must be recreated here since stop() destroys them completely
        mIoContext = std::make_unique<boost::asio::io_context>();
        mBusinessPool = std::make_unique<boost::asio::thread_pool>(std::max(8u, std::thread::hardware_concurrency()));
        
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 0);
        mAcceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(*mIoContext, endpoint);
        
        boost::asio::ip::tcp::endpoint actualEndpoint = mAcceptor->local_endpoint();
        unsigned short port = actualEndpoint.port();
        
        // Port file allows clients to discover the server's listening port
        std::string portFile = getIPCPortFilePath();
        boost::nowide::ofstream file(portFile);
        if (!file.is_open()) {
            BOOST_LOG_TRIVIAL(error) << "IPCServer: failed to write port file: " << portFile;
            cleanupResources();
            return;
        }
        file << port;
        file.close();
        BOOST_LOG_TRIVIAL(info) << "IPCServer: listening on port " << port << ", port info saved to " << portFile;

        mRunning.store(true);
        acceptNext();

        UserNetworkEvent::getInstance()->userInfoChanged.connect([this](const UserInfoChangedEvent&) {
            broadcastEvent(IPCEvent("user.userInfoChanged", nlohmann::json::object(), ""));
        });
        
        int numThreads = std::max(2, static_cast<int>(std::thread::hardware_concurrency() / 4));
        for (int i = 0; i < numThreads; ++i) {
            mIoThreads.emplace_back([this]() { mIoContext->run(); });
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "IPCServer: start failed: " << e.what();
        cleanupResources();
        mRunning.store(false);
    }
}

void IPCServer::stop()
{
    std::lock_guard<std::mutex> lock(mServerMutex);
    
    if (!mRunning.load())
        return;

    mRunning.store(false);

    UserNetworkEvent::getInstance()->userInfoChanged.disconnectAll();

    std::string portFile = getIPCPortFilePath();
    if (boost::nowide::remove(portFile.c_str()) != 0) {
        BOOST_LOG_TRIVIAL(debug) << "IPCServer: failed to remove port file: " << portFile;
    }

    cleanupResources();
}

void IPCServer::cleanupResources()
{
    if (mAcceptor) {
        boost::system::error_code ec;
        mAcceptor->close(ec);
        mAcceptor.reset();
    }

    {
        std::lock_guard<std::mutex> lock(mSessionsMutex);
        for (auto it = mSessions.begin(); it != mSessions.end(); ++it) {
            if (auto session = it->lock()) {
                session->closeSync();
            }
        }
        mSessions.clear();
    }

    if (mBusinessPool) {
        mBusinessPool->stop();
        mBusinessPool->join();
        mBusinessPool.reset();
    }

    if (mIoContext) {
        mIoContext->stop();
    }

    for (auto& thread : mIoThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    mIoThreads.clear();

    if (mIoContext) {
        mIoContext.reset();
    }
}


void IPCServer::acceptNext()
{
    if (!mRunning.load()) {
        BOOST_LOG_TRIVIAL(debug) << "IPCServer: accept ignored (stopping)";
        return;
    }

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(*mIoContext);

    mAcceptor->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (!ec && mRunning.load()) {
            try {
                boost::asio::ip::tcp::endpoint remoteEndpoint = socket->remote_endpoint();
                BOOST_LOG_TRIVIAL(info) << "IPCServer: client connected from " 
                                       << remoteEndpoint.address().to_string() 
                                       << ":" << remoteEndpoint.port();
            } catch (const boost::system::system_error& e) {
                BOOST_LOG_TRIVIAL(warning) << "IPCServer: failed to get client endpoint: " << e.what();
            }
            auto session = std::make_shared<Session>(*mIoContext, *mBusinessPool, this, socket);
            session->start();
        } else if (!ec && !mRunning.load()) {
            BOOST_LOG_TRIVIAL(debug) << "IPCServer: accept ignored (stopping)";
        } else if (ec && ec != boost::asio::error::operation_aborted) {
            BOOST_LOG_TRIVIAL(warning) << "IPCServer: accept error: " << ec.message();
        }

        if (mRunning.load()) {
            acceptNext();
        }
    });
}

IPCResponse IPCServer::handleRequest(const IPCRequest& request)
{
    IPCResponse response;
    response.id     = request.id;
    response.method = request.method;

    try {
        std::string namesp, actualMethod;
        size_t      dotPos = request.method.find('.');
        if (dotPos != std::string::npos) {
            namesp       = request.method.substr(0, dotPos);
            actualMethod = request.method.substr(dotPos + 1);
        } else {
            namesp       = "unknown";
            actualMethod = request.method;
        }

        if (namesp == "printer") {
            if (actualMethod == "getPrinterNetworkInfo") {
                auto printerInfo = PrinterManager::getInstance()->getPrinterNetworkInfo(request.params["printerId"]);
                response.data    = convertPrinterNetworkInfoToJson(printerInfo);
            } else if (actualMethod == "getPrinterList") {
                auto           printers     = PrinterManager::getInstance()->getPrinterList();
                nlohmann::json printerArray = nlohmann::json::array();
                for (const auto& printer : printers) {
                    printerArray.push_back(convertPrinterNetworkInfoToJson(printer));
                }
                response.data = printerArray;
            } else if (actualMethod == "getSelectedPrinter") {
                auto printerInfo = PrinterManager::getInstance()->getSelectedPrinter(request.params["printerModel"],
                                                                                     request.params["printerId"]);
                response.data    = convertPrinterNetworkInfoToJson(printerInfo);
            } else if (actualMethod == "discoverPrinter") {
                auto result      = PrinterManager::getInstance()->discoverPrinter();
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData()) {
                    nlohmann::json printerArray = nlohmann::json::array();
                    for (const auto& printer : *result.data) {
                        printerArray.push_back(convertPrinterNetworkInfoToJson(printer));
                    }
                    response.data = printerArray;
                }
            } else if (actualMethod == "cancelBindPrinter") {
                PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(request.params);
                auto               result      = PrinterManager::getInstance()->cancelBindPrinter(printerInfo);
                response.code                  = static_cast<int>(result.code);
                response.message               = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "addPrinter") {
                PrinterNetworkInfo printer = convertJsonToPrinterNetworkInfo(request.params);
                auto               result  = PrinterManager::getInstance()->addPrinter(printer);
                response.code              = static_cast<int>(result.code);
                response.message           = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "deletePrinter") {
                auto result      = PrinterManager::getInstance()->deletePrinter(request.params["printerId"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "updatePrinterName") {
                auto result      = PrinterManager::getInstance()->updatePrinterName(request.params["printerId"], request.params["name"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "updatePrinterHost") {
                auto result      = PrinterManager::getInstance()->updatePrinterHost(request.params["printerId"], request.params["host"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "updatePhysicalPrinter") {
                PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(request.params["printerInfo"]);
                auto               result = PrinterManager::getInstance()->updatePhysicalPrinter(request.params["printerId"], printerInfo);
                response.code             = static_cast<int>(result.code);
                response.message          = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "getFileList") {
                auto result      = PrinterManager::getInstance()->getFileList(request.params["printerId"], request.params["pageNumber"],
                                                                              request.params["pageSize"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = convertPrinterPrintFileResponseToJson(*result.data);
            } else if (actualMethod == "getPrintTaskList") {
                auto result   = PrinterManager::getInstance()->getPrintTaskList(request.params["printerId"], request.params["pageNumber"],
                                                                                 request.params["pageSize"]);
                response.code = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = convertPrinterPrintTaskResponseToJson(*result.data);
            } else if (actualMethod == "deletePrintTasks") {
                std::vector<std::string> taskIds;
                if (request.params["taskIds"].is_array()) {
                    for (const auto& taskId : request.params["taskIds"]) {
                        taskIds.push_back(taskId.get<std::string>());
                    }
                }
                auto result      = PrinterManager::getInstance()->deletePrintTasks(request.params["printerId"], taskIds);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "sendRtmMessage") {
                auto result      = PrinterManager::getInstance()->sendRtmMessage(request.params["printerId"], request.params["message"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "getFileDetail") {
                auto result      = PrinterManager::getInstance()->getFileDetail(request.params["printerId"], request.params["fileName"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = convertPrinterPrintFileResponseToJson(*result.data);
            } else if (actualMethod == "getLicenseExpiredDevices") {
                auto result      = PrinterManager::getInstance()->getLicenseExpiredDevices();
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData()) {
                    nlohmann::json devicesJson = nlohmann::json::array();
                    for (const auto& dev : *result.data) {
                        nlohmann::json item;
                        item["serialNumber"] = dev.serialNumber;
                        item["status"]       = dev.status;
                        devicesJson.push_back(item);
                    }
                    response.data = devicesJson;
                }
            } else if (actualMethod == "renewLicense") {
                std::string serialNumber = request.params.value("serialNumber", "");
                auto        result       = PrinterManager::getInstance()->renewLicense(serialNumber);
                response.code            = static_cast<int>(result.code);
                response.message         = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "refreshPrinterStatus") {
                std::string printerId = request.params.value("printerId", "");
                auto        result    = PrinterManager::getInstance()->refreshPrinterStatus(printerId);
                response.code         = static_cast<int>(result.code);
                response.message      = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "getPrinterStatusRaw") {
                std::string printerId = request.params.value("printerId", "");
                auto        result    = PrinterManager::getInstance()->getPrinterStatusRaw(printerId);
                response.code         = static_cast<int>(result.code);
                response.message      = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "enqueueWanSyncRequest") {
                PrinterManager::getInstance()->enqueueWanSyncRequest();
                response.code = static_cast<int>(PrinterNetworkErrorCode::SUCCESS);
                response.data = true;
            } else if (actualMethod == "getPrinterMmsInfo") {
                auto result      = PrinterManager::getInstance()->getPrinterMmsInfo(request.params["printerId"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = convertPrinterMmsGroupToJson(*result.data);
            } else if (actualMethod == "upload") {
                PrinterNetworkParams params = convertJsonToPrinterNetworkParams(request.params);
                auto result = PrinterUploadManager::getInstance()->startAsyncUpload(params);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData()) {
                    response.data["taskId"] = *result.data;
                }
            } else if (actualMethod == "getUploadTask") {
                auto result      = PrinterUploadManager::getInstance()->getUploadTask(request.params["taskId"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = convertUploadTaskInfoToJson(*result.data);
            } else if (actualMethod == "cancelUploadTask") {
                auto result      = PrinterUploadManager::getInstance()->cancelUploadTask(request.params["taskId"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else {
                response.code    = static_cast<int>(PrinterNetworkErrorCode::OPERATION_NOT_IMPLEMENTED);
                response.message = "unknown method: " + request.method;
            }
        } else if (namesp == "user") {
            if (actualMethod == "getUserInfo") {
                auto userInfo = UserNetworkManager::getInstance()->getUserInfo();
                response.data = convertUserNetworkInfoToJson(userInfo);
            } else if (actualMethod == "login") {
                UserNetworkInfo userInfo = convertJsonToUserNetworkInfo(request.params);
                UserNetworkManager::getInstance()->login(userInfo);
                response.code = static_cast<int>(PrinterNetworkErrorCode::SUCCESS);
                response.data = true;
            } else if (actualMethod == "logout") {
                UserNetworkManager::getInstance()->logout();
                response.code = static_cast<int>(PrinterNetworkErrorCode::SUCCESS);
                response.data = true;
            } else if (actualMethod == "getRtcToken") {
                auto result      = UserNetworkManager::getInstance()->getRtcToken();
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = convertUserNetworkInfoToJson(*result.data);
            } else if (actualMethod == "bindWANPrinter") {
                PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(request.params);
                auto               result      = UserNetworkManager::getInstance()->bindWANPrinter(printerInfo);
                response.code                  = static_cast<int>(result.code);
                response.message               = result.message;
                if (result.hasData())
                    response.data = convertPrinterNetworkInfoToJson(*result.data);
            } else if (actualMethod == "unbindWANPrinter") {
                auto result      = UserNetworkManager::getInstance()->unbindWANPrinter(request.params["serialNumber"]);
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "getUserBoundPrinters") {
                auto result      = UserNetworkManager::getInstance()->getUserBoundPrinters();
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData()) {
                    nlohmann::json printerArray = nlohmann::json::array();
                    for (const auto& printer : *result.data) {
                        printerArray.push_back(convertPrinterNetworkInfoToJson(printer));
                    }
                    response.data = printerArray;
                }
            } else if (actualMethod == "checkUserNeedReLogin") {
                auto result      = UserNetworkManager::getInstance()->checkUserNeedReLogin();
                response.code    = static_cast<int>(result.code);
                response.message = result.message;
                if (result.hasData())
                    response.data = *result.data;
            } else if (actualMethod == "refreshToken") {
                UserNetworkInfo userInfo          = convertJsonToUserNetworkInfo(request.params);
                auto            refreshedUserInfo = UserNetworkManager::getInstance()->refreshToken(userInfo);
                if (refreshedUserInfo.userId.empty()) {
                    response.code    = static_cast<int>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD);
                    response.message = "refreshToken failed";
                    response.data    = nlohmann::json::object();
                } else {
                    response.code = static_cast<int>(PrinterNetworkErrorCode::SUCCESS);
                    response.data = convertUserNetworkInfoToJson(refreshedUserInfo);
                }
            } else if (actualMethod == "checkUserAuthStatus") {
                UserNetworkInfo         requestUserInfo = convertJsonToUserNetworkInfo(request.params["requestUserInfo"]);
                PrinterNetworkErrorCode errorCode       = static_cast<PrinterNetworkErrorCode>(request.params["errorCode"].get<int>());
                UserNetworkManager::getInstance()->checkUserAuthStatus(requestUserInfo, errorCode);
                response.code = static_cast<int>(PrinterNetworkErrorCode::SUCCESS);
                response.data = true;
            } else {
                response.code    = static_cast<int>(PrinterNetworkErrorCode::OPERATION_NOT_IMPLEMENTED);
                response.message = "unknown method: " + request.method;
            }
        } else {
            response.code    = static_cast<int>(PrinterNetworkErrorCode::OPERATION_NOT_IMPLEMENTED);
            response.message = "unknown namespace: " + namesp;
        }

    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "handleRequest error: " << e.what();
        response.code    = static_cast<int>(PrinterNetworkErrorCode::UNKNOWN_ERROR);
        response.message = std::string("exception: ") + e.what();
        response.data    = nlohmann::json::object();
    }

    return response;
}

void IPCServer::broadcastEvent(const IPCEvent& event)
{
    if (!mRunning.load()) {
        BOOST_LOG_TRIVIAL(debug) << "IPCServer: broadcastEvent ignored (not running)";
        return;
    }

    if (mIoContext) {
        boost::asio::post(*mIoContext, [this, event]() {
            std::lock_guard<std::mutex> lock(mSessionsMutex);
            
            for (auto it = mSessions.begin(); it != mSessions.end();) {
                if (auto session = it->lock()) {
                    session->sendEvent(event);
                    ++it;
                } else {
                    it = mSessions.erase(it);
                }
            }
        });
    }
}

} // namespace Slic3r
