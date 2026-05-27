#pragma once

#include "Singleton.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace Slic3r {

/**
 * @brief Coordinator for multi-instance synchronization
 * Manages master instance election using file lock
 * Only master instance initializes network components
 */
class MultiInstanceCoordinator : public Singleton<MultiInstanceCoordinator> {
    friend class Singleton<MultiInstanceCoordinator>;
    
public:
    MultiInstanceCoordinator(const MultiInstanceCoordinator&) = delete;
    MultiInstanceCoordinator& operator=(const MultiInstanceCoordinator&) = delete;
    
    /**
     * @brief Initialize coordinator and attempt to become master instance
     * @return true if this instance is the master, false if it's a slave
     */
    bool init();
    
    /**
     * @brief Cleanup coordinator
     */
    void uninit();
    
    /**
     * @brief Check if this instance is the master
     */
    bool isMaster() const { return mIsMaster.load(); }
    
    /**
     * @brief Get the path to the master lock file
     * @note This method is thread-safe and can be called before init()
     */
    static std::string getMasterLockPath();
    
    /**
     * @brief Register callback for when instance becomes master
     */
    using MasterStatusCallback = std::function<void(bool isMaster)>;
    void registerMasterStatusCallback(MasterStatusCallback callback);
    
private:
    MultiInstanceCoordinator();
    ~MultiInstanceCoordinator();
    
    void monitorMasterLock();
    bool tryAcquireMasterLock();
    void releaseMasterLock();
    
    std::atomic<bool> mIsMaster{false};
    std::atomic<bool> mRunning{false};
    std::thread mMonitorThread;
    
    MasterStatusCallback mMasterStatusCallback;
    std::mutex mCallbackMutex;
    std::mutex mCvMutex;
    std::condition_variable mCv;
    
    mutable std::mutex mHandleMutex;  // Protects lock handle/fd access
    
    std::string mMasterLockPath;  // Cached lock file path
    
#ifdef _WIN32
    void* mMasterLockHandle;
#else
    int mMasterLockFd;
#endif
};

} // namespace Slic3r

