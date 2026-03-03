#include "MultiInstanceCoordinator.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>
#include <mutex>
#include <condition_variable>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <cstring>
#endif

namespace Slic3r {

namespace fs = boost::filesystem;

MultiInstanceCoordinator::MultiInstanceCoordinator()
#ifdef _WIN32
    : mMasterLockHandle(nullptr)
#else
    : mMasterLockFd(-1)
#endif
{
    mIsMaster.store(false);
    mRunning.store(false);
}

MultiInstanceCoordinator::~MultiInstanceCoordinator() {
    uninit();
}

bool MultiInstanceCoordinator::init() {
    // Cache lock file path at initialization
    mMasterLockPath = getMasterLockPath();
    
    // Try to acquire master lock
    if (tryAcquireMasterLock()) {
        mIsMaster.store(true);
        BOOST_LOG_TRIVIAL(info) << "MultiInstanceCoordinator: This instance is master";
        // Master instance doesn't need monitoring thread
        return true;
    } else {
        mIsMaster.store(false);
        BOOST_LOG_TRIVIAL(info) << "MultiInstanceCoordinator: This instance is slave (another instance is master)";
        
        // Start monitoring thread to detect when master exits
        mRunning.store(true);
        mMonitorThread = std::thread([this]() { monitorMasterLock(); });
        
        return false;
    }
}

void MultiInstanceCoordinator::uninit() {
    // Stop monitor thread
    mRunning.store(false);
    {
        std::lock_guard<std::mutex> lk(mCvMutex);
        mCv.notify_all();
    }
    
    // Release lock BEFORE joining to unblock any IO in monitor thread
    releaseMasterLock();
    mIsMaster.store(false);
    
    // Now safe to join (monitor won't block on file operations)
    if (mMonitorThread.joinable()) {
        mMonitorThread.join();
    }
}

std::string MultiInstanceCoordinator::getMasterLockPath() {
    // Always get current data_dir to support runtime path changes
    fs::path dataDir = fs::path(Slic3r::data_dir());
    fs::path path = dataDir / "cache" / "elegoo_master_instance.lock";
    return path.string();
}

bool MultiInstanceCoordinator::tryAcquireMasterLock() {
    std::string lockPath = mMasterLockPath.empty() ? getMasterLockPath() : mMasterLockPath;
    
    // Ensure parent directory exists
    fs::path lockFilePath(lockPath);
    try {
        if (!fs::exists(lockFilePath.parent_path())) {
            fs::create_directories(lockFilePath.parent_path());
        }
    } catch (const fs::filesystem_error& e) {
        // Ignore if directory already exists (concurrent creation)
        if (!fs::exists(lockFilePath.parent_path())) {
            BOOST_LOG_TRIVIAL(error) << "MultiInstanceCoordinator: Failed to create lock directory: " << e.what();
            return false;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "MultiInstanceCoordinator: Failed to ensure lock directory: " << e.what();
        return false;
    }
    
#ifdef _WIN32
    // Windows: open with full exclusive access (no sharing)
    HANDLE h = CreateFileW(
        boost::nowide::widen(lockPath).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, // no sharing - full exclusive
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION) {
            // another process has opened with exclusive access => master exists
            return false;
        }
        BOOST_LOG_TRIVIAL(error) << "MultiInstanceCoordinator: CreateFileW failed: " << error;
        return false;
    }
    
    // Successfully opened with exclusive access -> we are master
    std::lock_guard<std::mutex> lock(mHandleMutex);
    mMasterLockHandle = h;
    return true;
#else
    // POSIX: open file and use flock for exclusive non-blocking lock
    int fd = open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        BOOST_LOG_TRIVIAL(error) << "MultiInstanceCoordinator: Failed to open lock file: " << strerror(errno);
        return false;
    }
    
    // Try non-blocking exclusive flock with EINTR retry
    int flockResult;
    do {
        flockResult = flock(fd, LOCK_EX | LOCK_NB);
    } while (flockResult != 0 && errno == EINTR);
    
    if (flockResult == 0) {
        // We got the lock -> keep fd open
        std::lock_guard<std::mutex> lock(mHandleMutex);
        mMasterLockFd = fd;
        return true;
    } else {
        // Failed to get lock
        int err = errno;
        close(fd);
        if (err == EWOULDBLOCK || err == EAGAIN) {
            // Another process holds the lock
            return false;
        }
        BOOST_LOG_TRIVIAL(error) << "MultiInstanceCoordinator: flock failed: " << strerror(err);
        return false;
    }
#endif
}

void MultiInstanceCoordinator::releaseMasterLock() {
    std::lock_guard<std::mutex> lock(mHandleMutex);
    
#ifdef _WIN32
    if (mMasterLockHandle != nullptr && mMasterLockHandle != INVALID_HANDLE_VALUE) {
        // Close handle - this releases exclusivity
        CloseHandle(mMasterLockHandle);
        mMasterLockHandle = nullptr;
        // Don't delete lock file - avoid race with other instances
    }
#else
    if (mMasterLockFd >= 0) {
        // Close fd (automatically releases flock)
        close(mMasterLockFd);
        mMasterLockFd = -1;
    }
#endif
}

void MultiInstanceCoordinator::monitorMasterLock() {
    while (mRunning.load()) {
        {
            std::unique_lock<std::mutex> cv_lk(mCvMutex);
            mCv.wait_for(cv_lk, std::chrono::seconds(1), [this]() { return !mRunning.load(); });
        }
        
        if (!mRunning.load()) break;
        
        if (tryAcquireMasterLock()) {
            mIsMaster.store(true);
            BOOST_LOG_TRIVIAL(info) << "MultiInstanceCoordinator: Successfully became master instance";
            
            MasterStatusCallback cb;
            {
                std::lock_guard<std::mutex> g(mCallbackMutex);
                cb = mMasterStatusCallback;
            }
            
            if (cb) {
                try {
                    cb(true);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "MultiInstanceCoordinator: Exception in master status callback (promotion)";
                }
            }
            
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void MultiInstanceCoordinator::registerMasterStatusCallback(MasterStatusCallback callback) {
    std::lock_guard<std::mutex> g(mCallbackMutex);
    mMasterStatusCallback = std::move(callback);
}

} // namespace Slic3r

