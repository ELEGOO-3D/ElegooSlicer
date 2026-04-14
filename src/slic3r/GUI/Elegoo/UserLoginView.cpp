#include "UserLoginView.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/JsonUtils.hpp"
#include "slic3r/Utils/Elegoo/UserNetworkManager.hpp"
#include "slic3r/Utils/Elegoo/ElegooNetworkHelper.hpp"

#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <thread>

namespace Slic3r { namespace GUI {

std::atomic<bool> UserLoginView::s_isShown{false};

wxBEGIN_EVENT_TABLE(UserLoginView, ElegooDialog)
wxEND_EVENT_TABLE()

void UserLoginView::ShowLoginDialog()
{
    bool expected = false;
    if (!s_isShown.compare_exchange_strong(expected, true)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": UserLoginView already shown, ignore duplicate request";
        return;
    }
    
    UserLoginView* dialog = new UserLoginView(nullptr);
    dialog->ShowModal();
    delete dialog;
}

UserLoginView::UserLoginView(wxWindow* parent)
    : ElegooDialog(parent, _L("Login"), _L(""), 0)
{
    // Get login URL from network helper
    std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
    if (!networkHelper) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not create network helper";
        return;
    }
    
    std::string url = networkHelper->getLoginUrl();
    mLanguage = networkHelper->getLanguage();
    mRegion = networkHelper->getRegion();
    
    // Initialize WebView with network URL
    initWebView(url, WebViewUrlType::Network);
    
    // Set UserAgent (needs to be done after WebView is created)
    if (getBrowser()) {
        getBrowser()->SetUserAgent(networkHelper->getUserAgent());
    }
    
    // Set dialog size
    wxSize pSize = FromDIP(wxSize(496, 723));
    SetSize(pSize);
    CenterOnParent();
}

UserLoginView::~UserLoginView()
{
    s_isShown = false;
}

void UserLoginView::setupIPCHandlers()
{
    if (!getIpc())
        return;

    getIpc()->onRequest("report.userInfo", [this](const IPCRequest& request) {
        auto        data     = request.params;
        std::string     xxx  = data.dump();
        UserNetworkInfo userNetworkInfo;
        userNetworkInfo.userId = JsonUtils::safeGetString(data, "userId", "");
        userNetworkInfo.token = JsonUtils::safeGetString(data, "accessToken", "");
        userNetworkInfo.accessTokenExpireTime = JsonUtils::safeGetInt64(data, "accessTokenExpireTime", 0);
        userNetworkInfo.refreshToken = JsonUtils::safeGetString(data, "refreshToken", "");
        userNetworkInfo.refreshTokenExpireTime = JsonUtils::safeGetInt64(data, "refreshTokenExpireTime", 0);
        userNetworkInfo.openid = JsonUtils::safeGetString(data, "openid", "");
        userNetworkInfo.avatar = JsonUtils::safeGetString(data, "avatar", "");
        userNetworkInfo.email = JsonUtils::safeGetString(data, "email", "");
        userNetworkInfo.nickname = JsonUtils::safeGetString(data, "nickname", "");
        userNetworkInfo.hostType = PrintHost::get_print_host_type_str(PrintHostType::htElegooLink);
        userNetworkInfo.phone = JsonUtils::safeGetString(data, "phone", "");

        std::string bakId = std::to_string(JsonUtils::safeGetInt64(data, "bakId", -1));
        std::string avatarPath = JsonUtils::safeGetString(data, "avatarPath", "");
        nlohmann::json extraInfoJson=nlohmann::json::object();
        extraInfoJson["bakId"] = bakId;
        extraInfoJson["avatarPath"] = avatarPath;
        userNetworkInfo.extraInfo = extraInfoJson.dump();

        userNetworkInfo.region = mRegion;
        userNetworkInfo.language = mLanguage;

        auto now = std::chrono::system_clock::now();
        userNetworkInfo.lastTokenRefreshTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        userNetworkInfo.loginTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        if(!userNetworkInfo.userId.empty() && !userNetworkInfo.token.empty()) {
            userNetworkInfo.loginStatus = LOGIN_STATUS_LOGIN_SUCCESS;
        } else {
            return IPCResult::error();
        }
   
        UserNetworkManager::getInstance()->login(userNetworkInfo);

        CallAfter([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            EndModal(wxID_OK);
        });
      
        return IPCResult::success();
    });

    getIpc()->onRequest("report.websiteOpen", [](const IPCRequest& request) {
        auto        params = request.params;
        std::string url    = params.value("url", "");
        wxGetApp().CallAfter([url]() {
            wxLaunchDefaultBrowser(url);
        });
        return IPCResult::success();
    });

    getIpc()->onRequest("reload", [this](const IPCRequest& request) {
        if(isLoading()){
          return IPCResult::success();
        }
        std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
        if (networkHelper && getBrowser()) {
            std::string url = networkHelper->getLoginUrl();
            CallAfter([this, url]() {
                getBrowser()->LoadURL(url);
            });
        }
        return IPCResult::success();
    });
    getIpc()->onRequest("isLoading", [this](const IPCRequest& request) {
        nlohmann::json data = nlohmann::json::object();
        data["isLoading"] = isLoading();
        return IPCResult::success(data);
    });
}


void UserLoginView::onCloseWindow(wxCloseEvent& event)
{
    s_isShown = false;
    ElegooDialog::onCloseWindow(event);
}

}} // namespace Slic3r::GUI
