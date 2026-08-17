#include "UserNetworkManager.hpp"
#include "IPCClient.hpp"
#include "MultiInstanceCoordinator.hpp"
#include "PrinterCache.hpp"
#include "JsonUtils.hpp"
#include "UserDataStorage.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>
#include <boost/nowide/fstream.hpp>
#include "libslic3r/format.hpp"
#include "slic3r/Utils/Elegoo/PrinterNetworkEvent.hpp"
#include "slic3r/Utils/Elegoo/MultiInstanceCoordinator.hpp"

// Do not lock mInitMutex: uninit() joins mMonitorThread while APIs may still call this macro.
#define CHECK_INITIALIZED(returnVal) \
    do { \
        if (!mIsInitialized.load(std::memory_order_acquire)) { \
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": not initialized, return PRINTER_NETWORK_NOT_INITIALIZED"; \
            using ValueType = std::decay_t<decltype(returnVal)>; \
            return PrinterNetworkResult<ValueType>(PrinterNetworkErrorCode::PRINTER_NETWORK_NOT_INITIALIZED, returnVal); \
        } \
    } while (0)
namespace Slic3r {

namespace fs = boost::filesystem;

UserNetworkManager::UserNetworkManager() {}

UserNetworkManager::~UserNetworkManager() { uninit(); }

void UserNetworkManager::init()
{
    BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::init";
    // Only master instance initializes network components
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::init: non-master, skip";
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(mInitMutex);
    if (mIsInitialized.load(std::memory_order_acquire)) {
        BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::init: already initialized";
        return;
    }
    loadUserInfo();

    mLoggedInElsewhereHandlerId = UserNetworkEvent::getInstance()->loggedInElsewhereChanged.connect(
        [this](const UserLoggedInElsewhereEvent&) {
            // Hold the lock across snapshot + invalidate so an in-flight token refresh
            // cannot commit in between and cause the kick to be rejected as stale.
            std::lock_guard<std::recursive_mutex> lock(mUserMutex);
            BOOST_LOG_TRIVIAL(warning) << "UserNetworkManager: loggedInElsewhereChanged event received, user id: " << mUserInfo.userId;
            const UserNetworkInfo snapshot = mUserInfo;
            updateUserInfoLoginStatus(snapshot, LOGIN_STATUS_OFFLINE_INVALID_TOKEN);
            mUserNetwork = nullptr;
        });
    mOnlineStatusChangedHandlerId = UserNetworkEvent::getInstance()->onlineStatusChanged.connect(
        [this](const UserOnlineStatusChangedEvent& event) {
            // May emit rapidly when the link flaps; offline uses warning for visibility at default warning filter.
            if (event.isOnline) {
                BOOST_LOG_TRIVIAL(info) << "UserNetworkManager: onlineStatusChanged, online, user id: " << getUserInfo().userId;
            } else {
                BOOST_LOG_TRIVIAL(warning) << "UserNetworkManager: onlineStatusChanged, offline, user id: " << getUserInfo().userId;
            }
            if (!event.isOnline) {
                updateUserInfoLoginStatus(getUserInfo(), LOGIN_STATUS_OFFLINE);
            }
        });
    mIsInitialized.store(true, std::memory_order_release);
    mRunning.store(true, std::memory_order_release);
    mMonitorThread = std::thread([this]() { monitorUserNetwork(); });
    BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::init: complete";
}

void UserNetworkManager::uninit()
{
    BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::uninit";
    {
        std::lock_guard<std::recursive_mutex> lock(mInitMutex);
        if (!mIsInitialized.load(std::memory_order_acquire)) {
            BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::uninit: not initialized, skip";
            return;
        }
    }

    if (mLoggedInElsewhereHandlerId != 0) {
        UserNetworkEvent::getInstance()->loggedInElsewhereChanged.disconnect(mLoggedInElsewhereHandlerId);
        mLoggedInElsewhereHandlerId = 0;
    }
    if (mOnlineStatusChangedHandlerId != 0) {
        UserNetworkEvent::getInstance()->onlineStatusChanged.disconnect(mOnlineStatusChangedHandlerId);
        mOnlineStatusChangedHandlerId = 0;
    }

    mRunning.store(false, std::memory_order_release);
    if (mMonitorThread.joinable()) {
        mMonitorThread.join();
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mInitMutex);
        std::lock_guard<std::recursive_mutex> userLock(mUserMutex);
        mUserInfo    = UserNetworkInfo();
        mUserNetwork = nullptr;
        mIsInitialized.store(false, std::memory_order_release);
    }
    BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::uninit: complete";
}

PrinterNetworkResult<UserNetworkInfo> UserNetworkManager::getRtcToken()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getRtcToken();
    }

    CHECK_INITIALIZED(UserNetworkInfo());
    std::shared_ptr<IUserNetwork> network = getNetwork();
    if (!network) {
        const UserNetworkInfo userInfo = getUserInfo();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": no network attached, user id: %1%, login status: %2%") % userInfo.userId %
                                          userInfo.loginStatus;
        if (userInfo.userId.empty()) {
            return PrinterNetworkResult<UserNetworkInfo>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD, UserNetworkInfo());
        }
        return PrinterNetworkResult<UserNetworkInfo>(PrinterNetworkErrorCode::NETWORK_ERROR, UserNetworkInfo());
    }
    // Record request context before sending request
    UserNetworkInfo requestUserInfo = network->getUserNetworkInfo();

    PrinterNetworkResult<UserNetworkInfo> rtcTokenResult = network->getRtcToken();
    if (rtcTokenResult.isSuccess()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(": success, user id: %1%") % requestUserInfo.userId;
    } else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": API failed, user id: %1%, error code: %2%, message: %3%") % requestUserInfo.userId %
                                          static_cast<int>(rtcTokenResult.code) % rtcTokenResult.message;
        // Pass request context for validation
        checkUserAuthStatus(requestUserInfo, rtcTokenResult.code);
    }
    return rtcTokenResult;
}

PrinterNetworkResult<PrinterNetworkInfo> UserNetworkManager::bindWANPrinter(const PrinterNetworkInfo& printerNetworkInfo)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->bindWANPrinter(printerNetworkInfo);
    }

    CHECK_INITIALIZED(PrinterNetworkInfo());
    std::shared_ptr<IUserNetwork> network = getNetwork();
    if (!network) {
        const UserNetworkInfo userInfo = getUserInfo();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": no network attached, user id: %1%, login status: %2%, serial number: %3%") %
                                          userInfo.userId % userInfo.loginStatus % printerNetworkInfo.serialNumber;
        if (userInfo.userId.empty()) {
            return PrinterNetworkResult<PrinterNetworkInfo>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD, PrinterNetworkInfo());
        }
        return PrinterNetworkResult<PrinterNetworkInfo>(PrinterNetworkErrorCode::NETWORK_ERROR, PrinterNetworkInfo());
    }
    // Record request context before sending request
    UserNetworkInfo requestUserInfo = network->getUserNetworkInfo();

    PrinterNetworkResult<PrinterNetworkInfo> result = network->bindWANPrinter(printerNetworkInfo);
    if (!result.isSuccess()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": API failed, user id: %1%, serial number: %2%, error code: %3%, message: %4%") %
                                          requestUserInfo.userId % printerNetworkInfo.serialNumber % static_cast<int>(result.code) %
                                          result.message;
        // Pass request context for validation
        checkUserAuthStatus(requestUserInfo, result.code);
    }
    return result;
}
PrinterNetworkResult<bool> UserNetworkManager::unbindWANPrinter(const std::string& serialNumber)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->unbindWANPrinter(serialNumber);
    }

    CHECK_INITIALIZED(false);
    std::shared_ptr<IUserNetwork> network = getNetwork();
    if (!network) {
        const UserNetworkInfo userInfo = getUserInfo();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": no network attached, user id: %1%, login status: %2%, serial number: %3%") %
                                          userInfo.userId % userInfo.loginStatus % serialNumber;
        if (userInfo.userId.empty()) {
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD, false);
        }
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }
    UserNetworkInfo            requestUserInfo = network->getUserNetworkInfo();
    PrinterNetworkResult<bool> result          = network->unbindWANPrinter(serialNumber);
    if (!result.isSuccess()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": API failed, user id: %1%, serial number: %2%, error code: %3%, message: %4%") %
                                          requestUserInfo.userId % serialNumber % static_cast<int>(result.code) % result.message;
        // Pass request context for validation
        checkUserAuthStatus(requestUserInfo, result.code);
    }
    return result;
}

PrinterNetworkResult<std::vector<PrinterNetworkInfo>> UserNetworkManager::getUserBoundPrinters()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getUserBoundPrinters();
    }

    CHECK_INITIALIZED(std::vector<PrinterNetworkInfo>());

    std::unique_lock<std::timed_mutex> lock(mMonitorMutex, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::seconds(3))) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": failed to acquire lock after 3 seconds, user network busy";
        return PrinterNetworkResult<std::vector<PrinterNetworkInfo>>(PrinterNetworkErrorCode::USER_NETWORK_BUSY,
                                                                     std::vector<PrinterNetworkInfo>());
    }

    std::shared_ptr<IUserNetwork> network = getNetwork();
    if (!network) {
        UserNetworkInfo userInfo = getUserInfo();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": no network attached, user id: %1%, login status: %2%") % userInfo.userId %
                                          userInfo.loginStatus;
        if (userInfo.userId.empty()) {
            return PrinterNetworkResult<std::vector<PrinterNetworkInfo>>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD,
                                                                         std::vector<PrinterNetworkInfo>());
        }
        return PrinterNetworkResult<std::vector<PrinterNetworkInfo>>(PrinterNetworkErrorCode::NETWORK_ERROR,
                                                                     std::vector<PrinterNetworkInfo>());
    }

    // Record request context before sending request
    UserNetworkInfo requestUserInfo = network->getUserNetworkInfo();

    PrinterNetworkResult<std::vector<PrinterNetworkInfo>> printersResult = network->getUserBoundPrinters();
    if (!printersResult.isSuccess()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": API failed, user id: %1%, error code: %2%, message: %3%") % requestUserInfo.userId %
                                          static_cast<int>(printersResult.code) % printersResult.message;
        // Pass request context for validation
        checkUserAuthStatus(requestUserInfo, printersResult.code);
    }
    return printersResult;
}

PrinterNetworkResult<std::vector<LicenseExpiredDevice>> UserNetworkManager::getLicenseExpiredDevices()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getLicenseExpiredDevices();
    }

    CHECK_INITIALIZED(std::vector<LicenseExpiredDevice>());
    std::shared_ptr<IUserNetwork> network = getNetwork();
    if (!network) {
        const UserNetworkInfo userInfo = getUserInfo();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": no network attached, user id: %1%, login status: %2%") % userInfo.userId %
                                          userInfo.loginStatus;
        if (userInfo.userId.empty()) {
            return PrinterNetworkResult<std::vector<LicenseExpiredDevice>>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD,
                                                                           std::vector<LicenseExpiredDevice>());
        }
        return PrinterNetworkResult<std::vector<LicenseExpiredDevice>>(PrinterNetworkErrorCode::NETWORK_ERROR,
                                                                       std::vector<LicenseExpiredDevice>());
    }
    UserNetworkInfo                                         requestUserInfo = network->getUserNetworkInfo();
    PrinterNetworkResult<std::vector<LicenseExpiredDevice>> result          = network->getLicenseExpiredDevices();
    if (!result.isSuccess()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": API failed, user id: %1%, error code: %2%, message: %3%") % requestUserInfo.userId %
                                          static_cast<int>(result.code) % result.message;
        checkUserAuthStatus(requestUserInfo, result.code);
    }
    return result;
}

PrinterNetworkResult<bool> UserNetworkManager::renewLicense(const std::string& serialNumber)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->renewLicense(serialNumber);
    }

    CHECK_INITIALIZED(false);
    std::shared_ptr<IUserNetwork> network = getNetwork();
    if (!network) {
        const UserNetworkInfo userInfo = getUserInfo();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": no network attached, user id: %1%, login status: %2%, serial number: %3%") %
                                          userInfo.userId % userInfo.loginStatus % serialNumber;
        if (userInfo.userId.empty()) {
            return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD, false);
        }
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }
    UserNetworkInfo            requestUserInfo = network->getUserNetworkInfo();
    PrinterNetworkResult<bool> result          = network->renewLicense(serialNumber);
    if (!result.isSuccess()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": API failed, user id: %1%, serial number: %2%, error code: %3%, message: %4%") %
                                          requestUserInfo.userId % serialNumber % static_cast<int>(result.code) % result.message;
        checkUserAuthStatus(requestUserInfo, result.code);
    }
    return result;
}

void UserNetworkManager::checkUserAuthStatus(const UserNetworkInfo& requestUserInfo, const PrinterNetworkErrorCode& errorCode)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        IPCClient::getInstance()->checkUserAuthStatus(requestUserInfo, errorCode);
        return;
    }

    UserNetworkInfo currentUserInfo = getUserInfo();

    // 1. Check if user changed
    if (currentUserInfo.userId != requestUserInfo.userId) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(
                                          ": user id is different, skip check network error, request userId: %s, current userId: %s") %
                                          requestUserInfo.userId % currentUserInfo.userId;
        return;
    }

    // 2. Check if token was changed (more precise than time check)
    if (!requestUserInfo.token.empty() && !currentUserInfo.token.empty() && requestUserInfo.token != currentUserInfo.token) {
        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__
            << boost::format(": token was changed after this request, ignore stale error (request token: %s..., current token: %s...)") %
                   requestUserInfo.token.substr(0, 10) % currentUserInfo.token.substr(0, 10);
        return;
    }

    // 3. Check if token was refreshed after this request was sent (lastTokenRefreshTime is different)
    if (currentUserInfo.lastTokenRefreshTime != requestUserInfo.lastTokenRefreshTime) {
        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__
            << boost::format(
                   ": token was refreshed after this request (request time: %llu, current refresh time: %llu), ignore stale error") %
                   requestUserInfo.lastTokenRefreshTime % currentUserInfo.lastTokenRefreshTime;
        return;
    }

    // 4. Parse and update login status
    LoginStatus loginStatus = parseLoginStatusByErrorCode(errorCode);
    if (loginStatus != currentUserInfo.loginStatus) {
        if (loginStatus == LOGIN_STATUS_OFFLINE_TOKEN_EXPIRED || loginStatus == LOGIN_STATUS_OFFLINE ||
            loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": update user login status from %d to %d due to network error code %d") %
                                              currentUserInfo.loginStatus % loginStatus % static_cast<int>(errorCode);
            updateUserInfoLoginStatus(requestUserInfo, loginStatus);
        } else {
            // Other parsed statuses (e.g. INVALID_USER / OTHER_NETWORK_ERROR) are not
            // promoted to mUserInfo here; record so silent decisions can be traced.
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__
                << boost::format(": login status parsed to %d from error code %d, but not propagated (current status: %d, user id: %s)") %
                       loginStatus % static_cast<int>(errorCode) % currentUserInfo.loginStatus % currentUserInfo.userId;
        }
    }
}
UserNetworkInfo UserNetworkManager::getUserInfo()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->getUserInfo();
    }

    std::lock_guard<std::recursive_mutex> userLock(mUserMutex);
    UserNetworkInfo                       copy = mUserInfo;
    copy.loginErrorMessage                     = getLoginErrorMessage(copy);
    return copy;
}

PrinterNetworkResult<bool> UserNetworkManager::logout()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__;

    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        PrinterNetworkResult<bool> result = IPCClient::getInstance()->logout();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": delegated to IPCClient (slave)";
        return result;
    }

    std::lock_guard<std::recursive_mutex> lock(mUserMutex);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": logout user" << "userInfo.userId=" << mUserInfo.userId
    << "userInfo.username=" << mUserInfo.username << "nickname=" << mUserInfo.nickname
    << ", region=" << mUserInfo.region << "language=" << mUserInfo.language
    << ", loginStatus=" << mUserInfo.loginStatus;  

    mUserInfo = UserNetworkInfo();
    saveUserInfo(mUserInfo);
    notifyUserInfoUpdated();
    if (mUserNetwork) {
        mUserNetwork->logout();
        mUserNetwork = nullptr;
    }

    // request WAN printer list sync immediately
    PrinterNetworkEvent::getInstance()->printerOnlineListChanged.emit(PrinterOnlineListChangedEvent());
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": complete (master)";

    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}
PrinterNetworkResult<bool> UserNetworkManager::login(const UserNetworkInfo& userInfo)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": delegate to IPCClient (slave), userId=" << userInfo.userId;
        return IPCClient::getInstance()->login(userInfo);
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": login user (master), user id: %1%, nickname: %2%, region: %3%, host type: %4%") %
                                   userInfo.userId % userInfo.nickname % userInfo.region % userInfo.hostType;

    std::lock_guard<std::recursive_mutex> lock(mUserMutex);
    mUserInfo             = userInfo;
    mUserInfo.loginStatus = LOGIN_STATUS_LOGIN_SUCCESS;

    saveUserInfo(mUserInfo);
    notifyUserInfoUpdated();
    // Drop any stale network from a previous session.
    mUserNetwork = nullptr;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": complete (master), user id: " << userInfo.userId;
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
}

std::shared_ptr<IUserNetwork> UserNetworkManager::getNetwork() const
{
    std::lock_guard<std::recursive_mutex> lock(mUserMutex);
    return mUserNetwork;
}
void UserNetworkManager::setNetwork(std::shared_ptr<IUserNetwork> network)
{
    std::lock_guard<std::recursive_mutex> lock(mUserMutex);
    mUserNetwork = network;
}
bool UserNetworkManager::updateUserInfo(const UserNetworkInfo& userInfo)
{
    std::lock_guard<std::recursive_mutex> lock(mUserMutex);
    // if the user id is the same, update the user info
    if (mUserInfo.userId == userInfo.userId) {
        // Terminal status (invalid token/user) can only be left via login(); reject in-flight updates.
        if (mUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN || mUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": login status is invalid, skip update user info, user id: %s, login status: %d") %
                                              mUserInfo.userId % mUserInfo.loginStatus;
            return false;
        }
        bool needNotify = false;
        if (mUserInfo.loginStatus != userInfo.loginStatus) {
            needNotify = true;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                    << boost::format(": user login status updated, user id: %s, login status: %d") % userInfo.userId %
                                           userInfo.loginStatus;
        }
        if (mUserInfo.token != userInfo.token) {
            needNotify = true;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": user token updated, user id: %s, token: %s...") % userInfo.userId %
                                              userInfo.token.substr(0, 10);
        }
        if (mUserInfo.avatar != userInfo.avatar) {
            needNotify = true;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": user avatar updated, user id: %s, avatar length: %zu") % userInfo.userId %
                                              userInfo.avatar.size();
        }
        if (mUserInfo.nickname != userInfo.nickname) {
            needNotify = true;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": user nickname updated, user id: %s, nickname: %s") % userInfo.userId %
                                              userInfo.nickname;
        }
        mUserInfo                   = userInfo;
        mUserInfo.loginErrorMessage = getLoginErrorMessage(userInfo);
        if (needNotify) {
            notifyUserInfoUpdated();
        }
        saveUserInfo(mUserInfo);
        return true;
    }
    // user id is different
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << boost::format(": user id is different, skip update user info, user id: %s, new user id: %s") %
                                      mUserInfo.userId % userInfo.userId;
    return false;
}

bool UserNetworkManager::updateUserInfoLoginStatus(const UserNetworkInfo& userInfo, const LoginStatus& loginStatus)
{
    std::lock_guard<std::recursive_mutex> lock(mUserMutex);
    if (mUserInfo.userId != userInfo.userId) {
        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__
            << boost::format(": user id is different, skip update login status, user id: %s, new user id: %s, login status: %d") %
                   mUserInfo.userId % userInfo.userId % loginStatus;
        return false;
    }

    // Terminal status (invalid token/user) can only be left via login(); reject in-flight updates.
    if (mUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN || mUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": login status is invalid, skip update login status, user id: %s, login status: %d") %
                                          mUserInfo.userId % mUserInfo.loginStatus;
        return false;
    }
    if (mUserInfo.token != userInfo.token) {
        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__
            << boost::format(": token is different, skip update login status, user id: %s, new token: %s..., login status: %d") %
                   mUserInfo.userId % userInfo.token.substr(0, 10) % loginStatus;
        return false;
    }

    if (mUserInfo.lastTokenRefreshTime != userInfo.lastTokenRefreshTime) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": last token refresh time is different, skip update login status, user id: %s, new "
                                                    "last token refresh time: %llu, login status: %d") %
                                          mUserInfo.userId % userInfo.lastTokenRefreshTime % loginStatus;
        return false;
    }

    if (mUserInfo.userId.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": user id is empty, skip update login status, login status: %d") % loginStatus;
        return false;
    }

    if (mUserInfo.loginStatus != loginStatus) {
        mUserInfo.loginStatus       = loginStatus;
        mUserInfo.loginErrorMessage = getLoginErrorMessage(mUserInfo);
        notifyUserInfoUpdated();
        saveUserInfo(mUserInfo);
    }
    return true;
}
void UserNetworkManager::notifyUserInfoUpdated()
{
    // Cross-process state synchronization must not depend on the local UI lifecycle.
    if (MultiInstanceCoordinator::getInstance()->isMaster()) {
        UserNetworkEvent::getInstance()->userInfoChanged.emit(UserInfoChangedEvent());
    }

    const std::string userId      = mUserInfo.userId;
    const LoginStatus loginStatus = mUserInfo.loginStatus;
    wxGetApp().CallAfter([userId, loginStatus]() {
        if (wxGetApp().mainframe && wxGetApp().mainframe->is_loaded()) {
            auto evt = new wxCommandEvent(EVT_USER_INFO_UPDATED);
            wxQueueEvent(wxGetApp().mainframe, evt);
            BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::notifyUserInfoUpdated"
                                    << boost::format(": user info updated, send event to mainframe, user id: %s, login status: %d") %
                                           userId % loginStatus;
        } else {
            BOOST_LOG_TRIVIAL(info) << "UserNetworkManager::notifyUserInfoUpdated"
                                    << boost::format(": mainframe is not loaded, skip sending event, user id: %s, login status: %d") %
                                           userId % loginStatus;
        }
    });
}

PrinterNetworkResult<bool> UserNetworkManager::checkUserNeedReLogin()
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->checkUserNeedReLogin();
    }

    CHECK_INITIALIZED(false);
    UserNetworkInfo currentUserInfo = getUserInfo();
    if (needReLogin(currentUserInfo)) {
        // need re-login
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, true);
    } else if (currentUserInfo.loginStatus == LOGIN_STATUS_OFFLINE || currentUserInfo.loginStatus == LOGIN_STATUS_OTHER_NETWORK_ERROR ||
               currentUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_TOKEN_EXPIRED) {
        // network error or token expired, don't need to re-login, return network error message
        return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::NETWORK_ERROR, false);
    }
    // don't need to re-login
    return PrinterNetworkResult<bool>(PrinterNetworkErrorCode::SUCCESS, false);
}
std::string UserNetworkManager::getLoginErrorMessage(const UserNetworkInfo& userInfo)
{
    if (userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN) {
        return getErrorMessage(PrinterNetworkErrorCode::INVALID_TOKEN);
    } else if (userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
        return getErrorMessage(PrinterNetworkErrorCode::INVALID_USERNAME_OR_PASSWORD);
    } else if (userInfo.loginStatus == LOGIN_STATUS_OFFLINE) {
        return getErrorMessage(PrinterNetworkErrorCode::NETWORK_ERROR);
    }
    return "";
}

bool UserNetworkManager::needReLogin(const UserNetworkInfo& userInfo)
{
    return userInfo.userId.empty() || userInfo.token.empty() || userInfo.region.empty() || userInfo.loginStatus == LOGIN_STATUS_NO_USER ||
           userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN || userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER;
}
bool UserNetworkManager::checkTokenTimeInvalid(const UserNetworkInfo& userInfo)
{
    uint64_t nowTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    // refresh token expired
    if (userInfo.refreshTokenExpireTime <= nowTime) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": refresh token is expired, token time is invalid, user id: %s, user login status: "
                                                    "%d, user nickname: %s, refresh token expire time: %llu") %
                                          userInfo.userId % userInfo.loginStatus % userInfo.nickname %
                                          static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);
        return true;
    }

    // last token refresh time is greater than access token expire time, time is abnormal
    if (userInfo.lastTokenRefreshTime >= userInfo.accessTokenExpireTime) {
        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__
            << boost::format(
                   ": last token refresh time is greater than access token expire time, user id: %s, user login status: %d, user nickname: "
                   "%s, last token refresh time: %llu, access token expire time: %llu, refresh token expire time: %llu") %
                   userInfo.userId % userInfo.loginStatus % userInfo.nickname %
                   static_cast<unsigned long long>(userInfo.lastTokenRefreshTime) %
                   static_cast<unsigned long long>(userInfo.accessTokenExpireTime) %
                   static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);
        return true;
    }
    // nowTime is less than last token refresh time, time is abnormal
    if (nowTime < userInfo.lastTokenRefreshTime) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": nowTime is less than last token refresh time, need to refresh token, user id: %s, "
                                                    "user login status: %d, user nickname: %s, last token refresh time: %llu, nowTime: "
                                                    "%llu, access token expire time: %llu, refresh token expire time: %llu") %
                                          userInfo.userId % userInfo.loginStatus % userInfo.nickname %
                                          static_cast<unsigned long long>(userInfo.lastTokenRefreshTime) %
                                          static_cast<unsigned long long>(nowTime) %
                                          static_cast<unsigned long long>(userInfo.accessTokenExpireTime) %
                                          static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);
        return true;
    }
    return false;
}
bool UserNetworkManager::checkNeedRefreshToken(const UserNetworkInfo& userInfo)
{
    // if user login status is invalid token or invalid user, don't need to refresh token
    if (userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN || userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": user login status is invalid token or invalid user, don't need to refresh token, "
                                                    "user id: %s, user nickname: %s, user login status: %d") %
                                          userInfo.userId % userInfo.nickname % userInfo.loginStatus;
        return false;
    }

    if (checkTokenTimeInvalid(userInfo)) {
        return false;
    }

    // login status is token expired, need to refresh token
    if (userInfo.loginStatus == LOGIN_STATUS_OFFLINE_TOKEN_EXPIRED) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(
                                       ": user login status is token expired, need to refresh token, user id: %s, user nickname: %s, user "
                                       "login status: %d, access token expire time: %llu, refresh token expire time: %llu") %
                                       userInfo.userId % userInfo.nickname % userInfo.loginStatus %
                                       static_cast<unsigned long long>(userInfo.accessTokenExpireTime) %
                                       static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);
        return true;
    }

    // use UTC milliseconds to align with backend timestamps
    uint64_t nowTime = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    // token expired
    if (userInfo.accessTokenExpireTime <= nowTime) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": token expired, user id: %s, user login status: %d, user nickname: %s, access token "
                                                    "expire time: %llu, refresh token expire time: %llu") %
                                          userInfo.userId % userInfo.loginStatus % userInfo.nickname %
                                          static_cast<unsigned long long>(userInfo.accessTokenExpireTime) %
                                          static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);
        return true;
    }

    // Half of the token's validity period has passed since the last refresh
    uint64_t tokenValidDiffTime = userInfo.accessTokenExpireTime - userInfo.lastTokenRefreshTime;
    uint64_t elapsedTokenTime   = (nowTime > userInfo.lastTokenRefreshTime) ? (nowTime - userInfo.lastTokenRefreshTime) : 0ULL;
    if (elapsedTokenTime > tokenValidDiffTime / 2) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(": token valid time is not enough, user id: %s, user login status: %d, user nickname: %s, "
                                                 "access token expire time: %llu, refresh token expire time: %llu") %
                                       userInfo.userId % userInfo.loginStatus % userInfo.nickname %
                                       static_cast<unsigned long long>(userInfo.accessTokenExpireTime) %
                                       static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);
        return true;
    }
    return false;
}

UserNetworkInfo UserNetworkManager::refreshToken(const UserNetworkInfo& userInfo)
{
    if (!MultiInstanceCoordinator::getInstance()->isMaster()) {
        return IPCClient::getInstance()->refreshToken(userInfo);
    }

    if (!mIsInitialized.load(std::memory_order_acquire)) {
        return UserNetworkInfo();
    }
    std::lock_guard<std::timed_mutex> monitorLock(mMonitorMutex);

    UserNetworkInfo               currentUserInfo = getUserInfo();
    std::shared_ptr<IUserNetwork> network         = getNetwork();

    // redact sensitive tokens in logs; print timestamp fields with correct specifiers
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": start refresh token, user id: %s, login refresh time: %llu, access token expire time: "
                                             "%llu, refresh token expire time: %llu") %
                                   userInfo.userId % static_cast<unsigned long long>(userInfo.lastTokenRefreshTime) %
                                   static_cast<unsigned long long>(userInfo.accessTokenExpireTime) %
                                   static_cast<unsigned long long>(userInfo.refreshTokenExpireTime);

    // if current user id is empty, skip refresh token
    if (currentUserInfo.userId.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": current user id is empty, skip refresh token, current user id: %s") %
                                          currentUserInfo.userId;
        return UserNetworkInfo();
    }

    // if user login status is invalid token or invalid user, skip refresh token
    if (currentUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN ||
        currentUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(
                                          ": user login status is invalid token or invalid user, skip refresh token, current user id: %s") %
                                          currentUserInfo.userId;
        return UserNetworkInfo();
    }

    // if user id changed and user already logged in, return current user info
    if (userInfo.userId != currentUserInfo.userId && currentUserInfo.loginStatus == LOGIN_STATUS_LOGIN_SUCCESS) {
        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__
            << boost::format(
                   ": user id changed, current user id is not empty and user already logged in, return current user info, user id: %s") %
                   userInfo.userId;
        return currentUserInfo;
    }

    // if already refreshed token, skip refresh token
    if (currentUserInfo.lastTokenRefreshTime > userInfo.lastTokenRefreshTime && currentUserInfo.loginStatus == LOGIN_STATUS_LOGIN_SUCCESS) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(": already refreshed token, skip refresh token, user id: %s") % userInfo.userId;
        return currentUserInfo;
    }

    if (network && currentUserInfo.userId != network->getUserNetworkInfo().userId) {
        // user id changed
        network = nullptr;
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": user id changed, set network to nullptr, user id: %s") % userInfo.userId;
    }

    if (refreshToken(currentUserInfo, network)) {
        if (!updateUserInfo(currentUserInfo)) {
            // Status turned terminal while refresh was in flight; discard the new token/network.
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": user info rejected after refresh, drop network, user id: %s") %
                                              currentUserInfo.userId;
            setNetwork(nullptr);
            return getUserInfo();
        }
        setNetwork(network);
        BOOST_LOG_TRIVIAL(info)
            << __FUNCTION__
            << boost::format(
                   ": refresh token completed (network updated), user id: %s, accessTokenExpireTime: %llu, refreshTokenExpireTime: %llu") %
                   currentUserInfo.userId % static_cast<unsigned long long>(currentUserInfo.accessTokenExpireTime) %
                   static_cast<unsigned long long>(currentUserInfo.refreshTokenExpireTime);
        return currentUserInfo;
    } else if (currentUserInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN) {
        updateUserInfo(currentUserInfo);
        setNetwork(nullptr);
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": refresh token failed, need to re-login, user id: %s") % userInfo.userId;
        return UserNetworkInfo();
    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << boost::format(": refresh token failed, user id: %s, login status: %d") % userInfo.userId %
                                      userInfo.loginStatus;
    return currentUserInfo;
}

bool UserNetworkManager::refreshToken(UserNetworkInfo& userInfo, std::shared_ptr<IUserNetwork>& network)
{
    if (userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN || userInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": skip refresh, login status already terminal, user id: %1%, login status: %2%") %
                                          userInfo.userId % userInfo.loginStatus;
        return false;
    }

    if (!network) {
        network = NetworkFactory::createUserNetwork(userInfo);
        if (!network) {
            userInfo.loginStatus = LOGIN_STATUS_OFFLINE_INVALID_USER;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": user token refresh failed, network is null, user id: %s") % userInfo.userId;
            return false;
        }
        std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(
            PrintHost::get_print_host_type(userInfo.hostType));
        if (networkHelper) {
            network->setRegion(userInfo.region, networkHelper->getIotUrl());
        } else {
            userInfo.loginStatus = LOGIN_STATUS_OFFLINE_INVALID_USER;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << boost::format(": user token refresh failed, network helper is null, user id: %s, host type: %s") %
                                              userInfo.userId % userInfo.hostType;
            return false;
        }
    }

    auto refreshResult = network->refreshToken(userInfo);
    if (refreshResult.isSuccess() && refreshResult.hasData()) {
        UserNetworkInfo refreshedUser   = refreshResult.data.value();
        userInfo.token                  = refreshedUser.token;
        userInfo.refreshToken           = refreshedUser.refreshToken;
        userInfo.accessTokenExpireTime  = refreshedUser.accessTokenExpireTime;
        userInfo.refreshTokenExpireTime = refreshedUser.refreshTokenExpireTime;
        // set login status to login success
        userInfo.loginStatus = LOGIN_STATUS_LOGIN_SUCCESS;
        // set last token refresh time to current time milliseconds (UTC) to match backend
        userInfo.lastTokenRefreshTime = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        network->updateUserNetworkInfo(userInfo);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": user token refreshed successfully, user id: %s") % userInfo.userId;
        return true;
    }
    if (refreshResult.code != PrinterNetworkErrorCode::NETWORK_ERROR) {
        // if error code is not network error, token is invalid, need to re-login
        userInfo.loginStatus = LOGIN_STATUS_OFFLINE_INVALID_TOKEN;
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << boost::format(": user token refresh failed, need to re-login, user id: %s, error code: %d") %
                                          userInfo.userId % static_cast<int>(refreshResult.code);
        return false;
    }
    // if error code is network error, don't need to re-login, just set login status to offline
    if (userInfo.loginStatus != LOGIN_STATUS_OFFLINE_TOKEN_EXPIRED) {
        userInfo.loginStatus = LOGIN_STATUS_OFFLINE;
    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << boost::format(": user token refresh failed, user id: %s, status: %d, error code: %d, error message: %s") %
                                      userInfo.userId % userInfo.loginStatus % static_cast<int>(refreshResult.code) % refreshResult.message;
    return false;
}

void UserNetworkManager::monitorUserNetwork()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": monitor thread started";
    mLastLoopTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    while (mRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - mLastLoopTime).count();

        if (elapsed < 10) {
            continue;
        }

        // Guard the whole tick so a transient exception (e.g. backend timeout)
        // cannot kill the monitor thread and leave the user permanently offline.
        try {
            std::lock_guard<std::timed_mutex> lock(mMonitorMutex);

            mLastLoopTime                                 = now;
            UserNetworkInfo               userInfo        = getUserInfo();
            std::shared_ptr<IUserNetwork> network         = getNetwork();
            LoginStatus                   lastLoginStatus = userInfo.loginStatus;

            // Step 1: Check if need re-login
            if (needReLogin(userInfo)) {
                if(network) {
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                               << boost::format(": tick step 1, need re-login, user id: %1%, login status: %2%") %
                                                      userInfo.userId % userInfo.loginStatus;
                }
                setNetwork(nullptr);
                continue;
            }

            if (checkTokenTimeInvalid(userInfo)) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                           << boost::format(": tick step 1, token time invalid, mark INVALID_TOKEN, user id: %1%") %
                                                  userInfo.userId;
                updateUserInfoLoginStatus(userInfo, LOGIN_STATUS_OFFLINE_INVALID_TOKEN);
                setNetwork(nullptr);
                continue;
            }

            // Step 2: Check if user changed
            if (network && network->getUserNetworkInfo().userId != userInfo.userId) {
                BOOST_LOG_TRIVIAL(warning)
                    << __FUNCTION__
                    << boost::format(": tick step 2, user changed, drop network, attached user id: %1%, current user id: %2%") %
                           network->getUserNetworkInfo().userId % userInfo.userId;
                setNetwork(nullptr);
                continue;
            }

            // Step 3: Check if need refresh token
            if (checkNeedRefreshToken(userInfo)) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": tick step 3, refresh token, user id: %1%") % userInfo.userId;
                if (refreshToken(userInfo, network) && updateUserInfo(userInfo)) {
                    setNetwork(network);
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                            << boost::format(": tick step 3, refresh token succeeded, user id: %1%") % userInfo.userId;
                } else {
                    setNetwork(nullptr);
                    if (lastLoginStatus != userInfo.loginStatus) {
                        updateUserInfoLoginStatus(userInfo, userInfo.loginStatus);
                    }
                }
                continue;
            }

            // Step 4: If already logged in, sync printers and skip
            if (userInfo.loginStatus == LOGIN_STATUS_LOGIN_SUCCESS && network) {
                continue;
            }

            // Step 5: Connect to IoT
            BOOST_LOG_TRIVIAL(info)
                << __FUNCTION__
                << boost::format(": tick step 5, (re)connect to IoT, user id: %1%, login status: %2%, region: %3%, host type: %4%") %
                       userInfo.userId % userInfo.loginStatus % userInfo.region % userInfo.hostType;
            // set last network to nullptr to disconnect from IoT and avoid race condition
            setNetwork(nullptr);
            std::shared_ptr<IUserNetwork> newNetwork = NetworkFactory::createUserNetwork(userInfo);
            if (!newNetwork) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                           << boost::format(
                                                  ": createUserNetwork returned null, mark INVALID_USER, user id: %1%, host type: %2%") %
                                                  userInfo.userId % userInfo.hostType;
                if (lastLoginStatus != LOGIN_STATUS_OFFLINE_INVALID_USER) {
                    updateUserInfoLoginStatus(userInfo, LOGIN_STATUS_OFFLINE_INVALID_USER);
                }
                continue;
            }

            // different regions need different service addresses
            std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(
                PrintHost::get_print_host_type(userInfo.hostType));
            if (networkHelper) {
                newNetwork->setRegion(userInfo.region, networkHelper->getIotUrl());
            } else {
                userInfo.loginStatus = LOGIN_STATUS_OFFLINE_INVALID_USER;
                if (lastLoginStatus != LOGIN_STATUS_OFFLINE_INVALID_USER) {
                    updateUserInfoLoginStatus(userInfo, LOGIN_STATUS_OFFLINE_INVALID_USER);
                }
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                           << boost::format(
                                                  ": user connect to IoT failed, network helper is null, user id: %1%, host type: %2%") %
                                                  userInfo.userId % userInfo.hostType;
                continue;
            }

            auto loginResult = newNetwork->connectToIot(userInfo);
            if (loginResult.isSuccess()) {
                if (loginResult.hasData()) {
                    UserNetworkInfo loginUserInfo = loginResult.data.value();
                    if (!loginUserInfo.avatar.empty())
                        userInfo.avatar = loginUserInfo.avatar;
                    if (!loginUserInfo.nickname.empty())
                        userInfo.nickname = loginUserInfo.nickname;
                    if (!loginUserInfo.email.empty())
                        userInfo.email = loginUserInfo.email;

                    userInfo.loginStatus = LOGIN_STATUS_LOGIN_SUCCESS;
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                            << boost::format(": connect to IoT success, user id: %1%, nickname: %2%") % userInfo.userId %
                                                   userInfo.nickname;
                    if (updateUserInfo(userInfo)) {
                        setNetwork(newNetwork);
                        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                                << boost::format(": IoT session persisted, network attached, user id: %1%") %
                                                       userInfo.userId;
                    }
                    // request WAN printer list sync immediately
                    PrinterNetworkEvent::getInstance()->printerOnlineListChanged.emit(PrinterOnlineListChangedEvent());
                } else {
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                               << boost::format(
                                                      ": connect to IoT returned success but no data, mark INVALID_USER, user id: %1%") %
                                                      userInfo.userId;
                    if (lastLoginStatus != LOGIN_STATUS_OFFLINE_INVALID_USER) {
                        updateUserInfoLoginStatus(userInfo, LOGIN_STATUS_OFFLINE_INVALID_USER);
                    }
                }
            } else {
                LoginStatus status = parseLoginStatusByErrorCode(loginResult.code);
                BOOST_LOG_TRIVIAL(warning)
                    << __FUNCTION__
                    << boost::format(": connect to IoT failed, user id: %1%, error code: %2%, message: %3%, parsed login status: %4%") %
                           userInfo.userId % static_cast<int>(loginResult.code) % loginResult.message % status;
                if (lastLoginStatus != status) {
                    updateUserInfoLoginStatus(userInfo, status);
                }
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": monitor tick failed: %1%") % e.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": monitor tick failed: unknown exception";
        }
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": monitor thread exiting";
}

void UserNetworkManager::saveUserInfo(const UserNetworkInfo& userInfo)
{
    try {
        fs::path userDir = fs::path(Slic3r::data_dir()) / "user";
        if (!fs::exists(userDir)) {
            fs::create_directories(userDir);
        }

        std::string jsonString = convertUserNetworkInfoToJson(userInfo).dump(4);

#if ELEGOO_INTERNAL_TESTING
        fs::path                path = userDir / "user_info.json";
        boost::nowide::ofstream file(path.string());
        if (file.is_open()) {
            file << jsonString;
            file.close();
        } else {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to open file for writing";
        }
#else
        fs::path        path = userDir / "user_info";
        UserDataStorage storage(path.string());
        if (!storage.save(jsonString)) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to save encrypted user info";
        }
#endif
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to save user info: %s") % e.what();
    }
}

void UserNetworkManager::loadUserInfo()
{
    try {
        fs::path    userDir = fs::path(Slic3r::data_dir()) / "user";
        std::string jsonString;

#if ELEGOO_INTERNAL_TESTING
        fs::path path = userDir / "user_info.json";
        if (fs::exists(path)) {
            boost::nowide::ifstream file(path.string());
            if (file.is_open()) {
                nlohmann::json json;
                file >> json;
                file.close();
                jsonString = json.dump();
            }
        }
#else
        fs::path path = userDir / "user_info";
        if (fs::exists(path)) {
            UserDataStorage storage(path.string());
            jsonString = storage.load();
        }
#endif

        if (jsonString.empty()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": no saved user info at " << path.string();
            return;
        }

        nlohmann::json  json     = nlohmann::json::parse(jsonString);
        UserNetworkInfo userInfo = convertJsonToUserNetworkInfo(json);

        if (userInfo.userId.empty()) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": loaded user info but userId is empty, ignore";
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(mUserMutex);
        // Persisted terminal statuses (INVALID_TOKEN/INVALID_USER) are kept so the
        // user is reminded to re-login; everything else gets demoted to NOT_LOGIN
        // until monitorUserNetwork validates the token.
        if (userInfo.loginStatus != LOGIN_STATUS_OFFLINE_INVALID_TOKEN && userInfo.loginStatus != LOGIN_STATUS_OFFLINE_INVALID_USER) {
            userInfo.loginStatus = LOGIN_STATUS_NOT_LOGIN;
        }
        mUserInfo = userInfo;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                << boost::format(": loaded user info, user id: %1%, nickname: %2%, region: %3%, login status: %4%") %
                                       mUserInfo.userId % mUserInfo.nickname % mUserInfo.region % mUserInfo.loginStatus;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to load user info: %1%") % e.what();
    }
}

bool UserNetworkManager::hasPersistedAccount()
{
    try {
        fs::path    userDir = fs::path(Slic3r::data_dir()) / "user";
        std::string jsonString;
#if ELEGOO_INTERNAL_TESTING
        fs::path path = userDir / "user_info.json";
        if (fs::exists(path)) {
            boost::nowide::ifstream file(path.string());
            if (file.is_open()) {
                nlohmann::json json;
                file >> json;
                jsonString = json.dump();
            }
        }
#else
        fs::path path = userDir / "user_info";
        if (fs::exists(path)) {
            jsonString = UserDataStorage(path.string()).load();
        }
#endif
        if (jsonString.empty())
            return false;
        return !convertJsonToUserNetworkInfo(nlohmann::json::parse(jsonString)).userId.empty();
    } catch (const std::exception&) {
        return false;
    }
    return false;
}

} // namespace Slic3r
