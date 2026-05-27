#include "HomepageView.hpp"
#include <wx/webview.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/url.h>
#include <slic3r/Utils/WebviewIPCManager.h>
#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/MainFrame.hpp>
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <slic3r/GUI/Elegoo/UserLoginView.hpp>
#include <slic3r/Utils/Elegoo/UserNetworkManager.hpp>
#include <slic3r/Utils/Elegoo/ElegooNetworkHelper.hpp>
#include <slic3r/Utils/Elegoo/ElegooUtils.hpp>
#include "slic3r/Utils/JsonUtils.hpp"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
namespace Slic3r { namespace GUI {

namespace {

nlohmann::json makeDeviceIdEventData()
{
    nlohmann::json data = nlohmann::json::object();
    data["deviceId"] = Slic3r::ElegooUtils::getDeviceId();
    return data;
}

}

// ============================================================================
// HomepageView Base Class Implementation
// ============================================================================

HomepageView::HomepageView(wxWindow* parent, const wxString& name)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize), mName(name)
{}

// ============================================================================
// RecentHomepageView Implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(RecentHomepageView, HomepageView) EVT_WEBVIEW_LOADED(wxID_ANY, RecentHomepageView::onWebViewLoaded)
    EVT_WEBVIEW_ERROR(wxID_ANY, RecentHomepageView::onWebViewError) wxEND_EVENT_TABLE()

        RecentHomepageView::RecentHomepageView(wxWindow* parent)
    : HomepageView(parent, "recent"), mBrowser(nullptr), mIpc(nullptr)
{
    // SetDropTarget(nullptr);  // Commented out to enable drag-and-drop file support
    initUI();
}

RecentHomepageView::~RecentHomepageView() { }

void RecentHomepageView::initUI()
{
    // Create webview
    mBrowser = WebView::CreateWebView(this, "");

    if (!mBrowser) {
        wxMessageBox("Failed to create webview", "Error", wxOK | wxICON_ERROR);
        return;
    }
    if (mBrowser) {
        mBrowser->EnableContextMenu(true); 
        mBrowser->SetFocus();
    }
    // Bind(wxEVT_WEBVIEW_NAVIGATING, &RecentHomepageView::OnNavigationRequest, this);
    // Bind(wxEVT_WEBVIEW_NAVIGATED, &RecentHomepageView::OnNavigationComplete, this);
    // Remove WebView border completely
    mBrowser->SetWindowStyleFlag(wxNO_BORDER);

    // Set up layout
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(mBrowser, 1, wxEXPAND, 0);
    SetSizer(sizer);

    // Initialize IPC
    mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(mBrowser);

    setupIPCHandlers();

    // Load recent files page
    wxString url = wxString::Format("file://%s/web/homepage4/recent.html", from_u8(resources_dir()));
    mBrowser->LoadURL(url);

    mBrowser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));
}

void RecentHomepageView::initialize()
{
    // Additional initialization if needed
}

void RecentHomepageView::updateMode()
{
    // Update mode if needed
}
void RecentHomepageView::setupIPCHandlers()
{
    if (!mIpc)
        return;

    mIpc->onRequest("getRecentFiles", [this](const IPCRequest& request) { return handleGetRecentFiles(request.params); });

    mIpc->onRequest("clearRecentFiles", [this](const IPCRequest& request) { return handleClearRecentFiles(request.params); });

    mIpc->onEvent("openRecentFile", [this](const IPCEvent& event) { return handleOpenFile(event.data); });

    mIpc->onEvent("createNewProject", [this](const IPCEvent& event) { return handleCreateNewProject(event.data); });

    mIpc->onEvent("openProject", [this](const IPCEvent& event) { return handleOpenProject(event.data); });

    mIpc->onEvent("openFileInExplorer",
                    [this](const IPCEvent& event) { return handleOpenFileInExplorer(event.data); });

    mIpc->onRequest("removeFromRecent", [this](const IPCRequest& request) { return handleRemoveFromRecent(request.params); });
}

void RecentHomepageView::showRecentFiles(int images){
    boost::property_tree::wptree data;
    wxGetApp().mainframe->get_recent_projects(data, images);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [key, value] : data) {
        nlohmann::json fileJson;
        fileJson["path"]         = wxString(value.get<std::wstring>(L"path", L"")).ToUTF8();
        fileJson["project_name"] = wxString(value.get<std::wstring>(L"project_name", L"")).ToUTF8();
        fileJson["time"]         = wxString(value.get<std::wstring>(L"time", L"")).ToUTF8();
        fileJson["image"]        = wxString(value.get<std::wstring>(L"image", L"")).ToUTF8();
        result.push_back(fileJson);
    }
    mIpc->sendEvent("recentFilesUpdated", result);
}
IPCResult RecentHomepageView::handleGetRecentFiles(const nlohmann::json&)
{
    int images= INT_MAX;
    boost::property_tree::wptree data;
    wxGetApp().mainframe->get_recent_projects(data, images);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& [key, value] : data) {
        nlohmann::json fileJson;
        fileJson["path"]         = wxString(value.get<std::wstring>(L"path", L"")).ToUTF8();
        fileJson["project_name"] = wxString(value.get<std::wstring>(L"project_name", L"")).ToUTF8();
        fileJson["time"]         = wxString(value.get<std::wstring>(L"time", L"")).ToUTF8();
        fileJson["image"]        = wxString(value.get<std::wstring>(L"image", L"")).ToUTF8();
        result.push_back(fileJson);
    }
    return IPCResult::success(result);
}

IPCResult RecentHomepageView::handleClearRecentFiles(const nlohmann::json& data)
{
    wxGetApp().CallAfter([]() {
        wxGetApp().request_remove_project("");
    });
    return IPCResult::success();
}

IPCResult RecentHomepageView::handleOpenFile(const nlohmann::json& data)
{
    std::string filePath = data.value("path", "");
    if (!filePath.empty()) {
        wxGetApp().CallAfter([filePath]() {
            wxGetApp().request_open_project(filePath);
        });
    }
    return IPCResult::success();
}

IPCResult RecentHomepageView::handleCreateNewProject(const nlohmann::json& data)
{
    // Use CallAfter to ensure this is executed in the main thread and after any pending UI operations
    wxGetApp().CallAfter([]() {
        wxGetApp().request_open_project("<new>");
    });
    return IPCResult::success();
}

IPCResult RecentHomepageView::handleOpenProject(const nlohmann::json& data)
{
    wxGetApp().CallAfter([]() {
        wxGetApp().request_open_project({});
    });
    return IPCResult::success();
}

IPCResult RecentHomepageView::handleOpenFileInExplorer(const nlohmann::json& data)
{
    std::string filePath = data.value("path", "");
    if (!filePath.empty()) {
        wxString wxFilePathStr = wxString::FromUTF8(filePath);
        // 打开文件夹
        wxFileName fileName(wxFilePathStr);
        wxString dirPath = fileName.GetPath();
        wxGetApp().CallAfter([this, dirPath]() {
            wxLaunchDefaultBrowser("file://" + dirPath);
        });
    }
    return IPCResult::success();
}

IPCResult RecentHomepageView::handleRemoveFromRecent(const nlohmann::json& data)
{
    std::string filePath = data.value("path", "");
    if (!filePath.empty()) {
        wxGetApp().CallAfter([filePath]() {
            wxGetApp().request_remove_project(filePath);
        });
    }
    return IPCResult::success();
}

void RecentHomepageView::OnNavigationRequest(wxWebViewEvent& evt){
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
}
void RecentHomepageView::OnNavigationComplete(wxWebViewEvent& evt){
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
}
void RecentHomepageView::onWebViewLoaded(wxWebViewEvent& event)
{
    // IPC is already initialized in constructor
    mIsReady = true;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": RecentHomepageView WebView loaded successfully";
}

void RecentHomepageView::onWebViewError(wxWebViewEvent& event)
{
    wxString error = event.GetString();
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": WebView Error: " << error.ToUTF8().data();
    // wxMessageBox("WebView Error: " + error, "Error", wxOK | wxICON_ERROR);
}

// ============================================================================
// OnlineModelsHomepageView Implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(OnlineModelsHomepageView, HomepageView) EVT_WEBVIEW_LOADED(wxID_ANY, OnlineModelsHomepageView::onWebViewLoaded)
    EVT_WEBVIEW_ERROR(wxID_ANY, OnlineModelsHomepageView::onWebViewError) wxEND_EVENT_TABLE()

        OnlineModelsHomepageView::OnlineModelsHomepageView(wxWindow* parent)
    : HomepageView(parent, "online-models"), mBrowser(nullptr), mIpc(nullptr)
{
    initUI();
}

OnlineModelsHomepageView::~OnlineModelsHomepageView() {  }

void OnlineModelsHomepageView::initUI()
{
    if (mBrowser != nullptr) {
        mBrowser->Destroy();
        mBrowser = nullptr;
    }
    // Create webview
    mBrowser = WebView::CreateWebView(this, "");

    if (!mBrowser) {
        wxMessageBox("Failed to create webview", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // Remove WebView border completely
    mBrowser->SetWindowStyleFlag(wxNO_BORDER);
    Bind(wxEVT_WEBVIEW_NAVIGATING, &OnlineModelsHomepageView::OnNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &OnlineModelsHomepageView::OnNavigationComplete, this);
    // Set up layout
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(mBrowser, 1, wxEXPAND, 0);
    SetSizer(sizer);

    // Initialize IPC
    mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(mBrowser);

    setupIPCHandlers();

    // Load online models page from remote URL
    std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
    if (!networkHelper) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not create network helper";
        return;
    }
    std::string url = networkHelper->getOnlineModelsUrl();
    mBrowser->SetUserAgent(networkHelper->getUserAgent());
    mBrowser->LoadURL(url);
    mBrowser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));
    Layout();
}

void OnlineModelsHomepageView::initialize()
{
    // Additional initialization if needed
}

void OnlineModelsHomepageView::updateMode()
{
    // Update mode if needed
}

nlohmann::json generateUserInfoData(const UserNetworkInfo& userNetworkInfo)
{
    nlohmann::json data;
    data["userId"]       = userNetworkInfo.userId;
    data["accessToken"]  = userNetworkInfo.token;
    data["refreshToken"] = userNetworkInfo.refreshToken;
    data["expiresTime"]  = userNetworkInfo.accessTokenExpireTime;
    data["refreshTokenExpireTime"] = userNetworkInfo.refreshTokenExpireTime;
    data["avatar"] = userNetworkInfo.avatar;
    data["email"] = userNetworkInfo.email;
    data["nickname"] = userNetworkInfo.nickname;
    data["loginStatus"]  = userNetworkInfo.loginStatus;
    data["lastTokenRefreshTime"] = userNetworkInfo.lastTokenRefreshTime;
    return data;
}

UserNetworkInfo parseUserInfoData(const nlohmann::json& data)
{
    UserNetworkInfo userNetworkInfo;
    userNetworkInfo.userId = JsonUtils::safeGetString(data, "userId", "");
    userNetworkInfo.refreshToken = JsonUtils::safeGetString(data, "refreshToken", "");
    userNetworkInfo.token = JsonUtils::safeGetString(data, "accessToken", "");
    userNetworkInfo.accessTokenExpireTime = JsonUtils::safeGetInt64(data, "expiresTime", 0);
    userNetworkInfo.refreshTokenExpireTime = JsonUtils::safeGetInt64(data, "refreshTokenExpireTime", 0);
    userNetworkInfo.lastTokenRefreshTime = JsonUtils::safeGetInt64(data, "lastTokenRefreshTime", 0);
    userNetworkInfo.loginStatus = LoginStatus(JsonUtils::safeGetInt(data, "loginStatus", static_cast<int>(LOGIN_STATUS_NO_USER)));
    userNetworkInfo.avatar = JsonUtils::safeGetString(data, "avatar", "");
    userNetworkInfo.email = JsonUtils::safeGetString(data, "email", "");
    userNetworkInfo.nickname = JsonUtils::safeGetString(data, "nickname", "");
    return userNetworkInfo;
}
void OnlineModelsHomepageView::setupIPCHandlers()
{
    if (!mIpc)
        return;

    mIpc->onRequest("report.getClientUserInfo", [this](const IPCRequest& request) {
        UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();
        if (userNetworkInfo.userId.empty() || userNetworkInfo.token.empty() ||
            userNetworkInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_TOKEN ||
            userNetworkInfo.loginStatus == LOGIN_STATUS_OFFLINE_INVALID_USER) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": OnlineModelsHomepageView user info is invalid, return empty user info";
            userNetworkInfo = UserNetworkInfo();
        }
        nlohmann::json  data = generateUserInfoData(userNetworkInfo);
        return IPCResult::success(data);
    });

    mIpc->onRequest("report.notLogged", [this](const IPCRequest& request) {
        UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();
        auto result = UserNetworkManager::getInstance()->checkUserNeedReLogin();
        if(result.isSuccess()) {
            bool needReLogin = result.data.value();
            if(needReLogin) {
                //need re-login
                auto evt = new wxCommandEvent(EVT_USER_LOGIN);
                wxQueueEvent(wxGetApp().mainframe, evt);
                return IPCResult::error();
            } 
        } else {
            //show_error(wxGetApp().mainframe, result.message);
            return IPCResult::error(result.message);
        }
        //don't need to re-login, return user info
        nlohmann::json  data = generateUserInfoData(userNetworkInfo);
        return IPCResult::success(data);
    });

    mIpc->onRequest("report.refreshToken", [this](const IPCRequest& request) { 
        auto        data     = request.params;
        UserNetworkInfo userNetworkInfo = parseUserInfoData(data);
        UserNetworkInfo refreshResult = UserNetworkManager::getInstance()->refreshToken(userNetworkInfo);
        return IPCResult::success(generateUserInfoData(refreshResult));       

    });
    mIpc->onRequest("report.ready", [this](const IPCRequest& request) { return handleReady(); });

    mIpc->onRequest("report.slicerOpen", [this](const IPCRequest& request) {
        auto        params = request.params;
        std::string url    = params.value("url", "");
        wxGetApp().CallAfter([url]() {
            GUI::wxGetApp().request_model_download(wxString(url));
        });
        return IPCResult::success();
    });
    mIpc->onRequest("report.websiteOpen", [this](const IPCRequest& request) {
        auto        params = request.params;
        std::string url    = params.value("url", "");
         wxGetApp().CallAfter([url]() {
            wxLaunchDefaultBrowser(url);
        });
        return IPCResult::success();
    });
    mIpc->onRequest("reload", [this](const IPCRequest& request) {
        std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
        if (networkHelper) {
            wxString url = from_u8(networkHelper->getOnlineModelsUrl());
            wxGetApp().CallAfter([this, url]() {
                if(mIsLoading){
                    return;
                }
                loadUrl(url);
            });
        }
        return IPCResult::success();
    });
    mIpc->onRequest("isLoading", [this](const IPCRequest& request) {
        nlohmann::json data = nlohmann::json::object();
        data["isLoading"] = mIsLoading;
        return IPCResult::success(data);
    });
}
void OnlineModelsHomepageView::onUserInfoUpdated(const UserNetworkInfo& userNetworkInfo)
{
    lock_guard<mutex> lock(mUserInfoMutex);
    // Update user info in webview
    if (!mIpc || !mIsReady) {
        mRefreshUserInfo = userNetworkInfo;
        return;
    }

    mRefreshUserInfo = UserNetworkInfo();
    nlohmann::json data = generateUserInfoData(userNetworkInfo);
    mIpc->sendEvent("client.onUserInfoUpdated", data, mIpc->generateRequestId());
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": OnlineModelsHomepageView sent user info to WebView";
}

void OnlineModelsHomepageView::onRegionChanged()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    //reload online models page
    mIsReady = false;
    mRefreshUserInfo = UserNetworkInfo();
    std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
    if (networkHelper) {
        wxString url = from_u8(networkHelper->getOnlineModelsUrl());
        wxGetApp().CallAfter([this, url]() {
            loadUrl(url);
        });
    }
}

void OnlineModelsHomepageView::onThemeChanged()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    //reload online models page
    mIsReady = false;
    mRefreshUserInfo = UserNetworkInfo();
    std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
    if (networkHelper) {
        wxString url = from_u8(networkHelper->getOnlineModelsUrl());
        wxGetApp().CallAfter([this, url]() {
            loadUrl(url);
        });
    }
}

IPCResult OnlineModelsHomepageView::handleReady()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    mIsReady = true;
    if (mIpc) {
        mIpc->sendEvent("client.setDeviceId", makeDeviceIdEventData(), mIpc->generateRequestId());
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": OnlineModelsHomepageView sent device id to WebView";
    }
    if (mIpc && !mRefreshUserInfo.userId.empty()) {
        nlohmann::json data = generateUserInfoData(mRefreshUserInfo);
        mIpc->sendEvent("client.onUserInfoUpdated", data, mIpc->generateRequestId());
        mRefreshUserInfo = UserNetworkInfo();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": OnlineModelsHomepageView sent user info to WebView";
    }
    return IPCResult::success();
}

void OnlineModelsHomepageView::onWebViewLoaded(wxWebViewEvent& event) {
    mIsLoading = false;
}
void OnlineModelsHomepageView::OnNavigationRequest(wxWebViewEvent& evt){
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    mIsLoading = true;
}
void OnlineModelsHomepageView::OnNavigationComplete(wxWebViewEvent& evt){
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    mIsLoading = false;
}
#define FAILED_URL_SUFFIX "/web/error-page/connection-failed.html"
void OnlineModelsHomepageView::onWebViewError(wxWebViewEvent& evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
    case wxWEBVIEW_NAV_ERR_CONNECTION: e = "wxWEBVIEW_NAV_ERR_CONNECTION"; break;
    case wxWEBVIEW_NAV_ERR_CERTIFICATE: e = "wxWEBVIEW_NAV_ERR_CERTIFICATE"; break;
    case wxWEBVIEW_NAV_ERR_AUTH: e = "wxWEBVIEW_NAV_ERR_AUTH"; break;
    case wxWEBVIEW_NAV_ERR_SECURITY: e = "wxWEBVIEW_NAV_ERR_SECURITY"; break;
    case wxWEBVIEW_NAV_ERR_NOT_FOUND: e = "wxWEBVIEW_NAV_ERR_NOT_FOUND"; break;
    case wxWEBVIEW_NAV_ERR_REQUEST: e = "wxWEBVIEW_NAV_ERR_REQUEST"; break;
    case wxWEBVIEW_NAV_ERR_USER_CANCELLED: e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED"; break;
    case wxWEBVIEW_NAV_ERR_OTHER: e = "wxWEBVIEW_NAV_ERR_OTHER"; break;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") % evt.GetURL() % evt.GetTarget() % e % evt.GetString();

    std::string url     = evt.GetURL().ToStdString();
    std::string target  = evt.GetTarget().ToStdString();
    std::string error   = e;
    std::string message = evt.GetString().ToStdString();
    mIsLoading = false;
    if(url.find(FAILED_URL_SUFFIX)!=std::string::npos){
        return;
    }

    // if (message == "COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED") {
    //     return;
    // }


    auto code = evt.GetInt();
    // if (code == wxWEBVIEW_NAV_ERR_CONNECTION || code == wxWEBVIEW_NAV_ERR_NOT_FOUND || code == wxWEBVIEW_NAV_ERR_REQUEST|| message == "wxWEBVIEW_NAV_ERR_AUTH") 
    {
        std::thread([this, url, target, error, message]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                wxGetApp().CallAfter([this]() {
                    loadFailedPage();
                });
        }).detach();
    }
}

void OnlineModelsHomepageView::loadFailedPage()
{
    auto path = resources_dir() + FAILED_URL_SUFFIX;
    #if WIN32
        std::replace(path.begin(), path.end(), '\\', '/');
    #endif

    if(!boost::filesystem::exists(path)){
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed page not found: " << path;
        return;
    }

    auto failedUrl = wxString::Format("file:///%s", from_u8(path));
    loadUrl(failedUrl);

}
void OnlineModelsHomepageView::loadUrl(const wxString& url){
    if(url.empty()){
        return;
    }
    wxString currentUrl = mBrowser->GetCurrentURL();
    if(url == currentUrl){
        return;
    }
    mCurrentUrl = url;
    if(mBrowser){
        mIsLoading = true;
        mBrowser->LoadURL(url);
        std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
        if (networkHelper) {
            mBrowser->SetUserAgent(networkHelper->getUserAgent());
        }
    }
}
}} // namespace Slic3r::GUI
