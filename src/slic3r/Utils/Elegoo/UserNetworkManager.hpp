#ifndef slic3r_UserNetworkManager_hpp_
#define slic3r_UserNetworkManager_hpp_

#include "Singleton.hpp"
#include "PrinterNetwork.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace Slic3r {

class UserNetworkManager : public Singleton<UserNetworkManager> {
    friend class Singleton<UserNetworkManager>;

public:
    UserNetworkManager(const UserNetworkManager&) = delete;
    UserNetworkManager& operator=(const UserNetworkManager&) = delete;

    void init();
    void uninit();
    
    UserNetworkInfo getUserInfo();
    // only can be called by login or logout
    void logout();
    void login(const UserNetworkInfo& userInfo);
    // for frontend click avatar, check user need re-login
    PrinterNetworkResult<bool> checkUserNeedReLogin();
    // for frontend refresh token
    UserNetworkInfo refreshToken(const UserNetworkInfo& userInfo);
    // for frontend
    PrinterNetworkResult<UserNetworkInfo> getRtcToken();

    // WAN
    PrinterNetworkResult<PrinterNetworkInfo> bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo);
    PrinterNetworkResult<bool>               unbindWANPrinter(const std::string& serialNumber);
    // printermanager get user bound printers
    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> getUserBoundPrinters();
    PrinterNetworkResult<std::vector<LicenseExpiredDevice>> getLicenseExpiredDevices();
    PrinterNetworkResult<bool>                              renewLicense(const std::string& serialNumber);
    // check user network error and update user info login status
    void checkUserAuthStatus(const UserNetworkInfo& requestUserInfo, const PrinterNetworkErrorCode& errorCode);

private:
    UserNetworkManager();
    ~UserNetworkManager();
    
    void monitorUserNetwork();
    bool refreshToken(UserNetworkInfo& userInfo, std::shared_ptr<IUserNetwork>& network);
    
    std::shared_ptr<IUserNetwork> getNetwork() const;
    void setNetwork(std::shared_ptr<IUserNetwork> network);
        
    void saveUserInfo(const UserNetworkInfo& userInfo);
    void loadUserInfo();
    bool updateUserInfo(const UserNetworkInfo& userInfo);
    bool updateUserInfoLoginStatus(const UserNetworkInfo& userInfo, const LoginStatus& loginStatus);
    bool needReLogin(const UserNetworkInfo& userInfo);

    std::string getLoginErrorMessage(const UserNetworkInfo& userInfo);
    void notifyUserInfoUpdated();

    bool checkNeedRefreshToken(const UserNetworkInfo& userInfo);
    bool checkTokenTimeInvalid(const UserNetworkInfo& userInfo);

private:
    mutable std::recursive_mutex mInitMutex;
    std::atomic<bool> mIsInitialized{false};
    
    mutable std::recursive_mutex mUserMutex;
    UserNetworkInfo mUserInfo;
    std::shared_ptr<IUserNetwork> mUserNetwork;
    
    std::timed_mutex mMonitorMutex;
    std::atomic<bool> mRunning{false};
    std::thread mMonitorThread;
    std::chrono::steady_clock::time_point mLastLoopTime;
    uint64_t mLoggedInElsewhereHandlerId{0};
    uint64_t mOnlineStatusChangedHandlerId{0};
};

} // namespace Slic3r

#endif
