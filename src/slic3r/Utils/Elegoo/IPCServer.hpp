#pragma once

#include "Singleton.hpp"
#include "slic3r/Utils/IPCMessage.hpp"
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <cstdint>
#include <vector>
#include <memory>
#include <set>
#include <mutex>

namespace Slic3r {

/**
 * IPC Server for master process
 *
 * Thread model:
 * - I/O thread pool: async socket operations only
 * - Business thread pool: request handlers may block
 * - Per-session strand: prevents concurrent writes to same socket
 */
class IPCServer : public Singleton<IPCServer>
{
    friend class Singleton<IPCServer>;

public:
    IPCServer(const IPCServer&)            = delete;
    IPCServer& operator=(const IPCServer&) = delete;

    void start();
    void stop();
    bool isRunning() const { return mRunning.load(); }  
    void broadcastEvent(const IPCEvent& event);
    
private:
    IPCServer();
    ~IPCServer();

    class Session;

    void        acceptNext();
    IPCResponse handleRequest(const IPCRequest& request);
    void        cleanupResources();
    std::unique_ptr<boost::asio::io_context>            mIoContext;
    std::unique_ptr<boost::asio::thread_pool>           mBusinessPool;
    std::unique_ptr<boost::asio::ip::tcp::acceptor>     mAcceptor;
    std::vector<std::thread>                            mIoThreads;
    std::atomic<bool>                                   mRunning{false};
    uint64_t                                            mUserInfoChangedHandlerId{0};
    std::mutex                                          mServerMutex;
    std::mutex                                          mSessionsMutex;
    std::set<std::weak_ptr<Session>, std::owner_less<>> mSessions;
};

} // namespace Slic3r
