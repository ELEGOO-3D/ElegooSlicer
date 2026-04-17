#include "HomeView.hpp"
#include "HomepageView.hpp"
#include <wx/webview.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <slic3r/Utils/WebviewIPCManager.h>
#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/Elegoo/UserLoginView.hpp>
#include <slic3r/GUI/MainFrame.hpp>
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <slic3r/Utils/Elegoo/UserNetworkManager.hpp>
#include <nlohmann/json.hpp>
#include <wx/url.h>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
namespace Slic3r { namespace GUI {

namespace {
constexpr int HOME_NAVIGATION_WIDTH_DIP = 240;
}

wxBEGIN_EVENT_TABLE(HomeView, wxPanel) EVT_WEBVIEW_LOADED(wxID_ANY, HomeView::onWebViewLoaded)
    EVT_WEBVIEW_ERROR(wxID_ANY, HomeView::onWebViewError) 
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, HomeView::OnNavigationRequest) 
    EVT_WEBVIEW_NAVIGATED(wxID_ANY, HomeView::OnNavigationComplete) 
    EVT_WEBVIEW_NEWWINDOW(wxID_ANY, HomeView::OnNewWindowRequest)
    wxEND_EVENT_TABLE()

        HomeView::HomeView(wxWindow* parent, wxWindowID id, const wxString& title)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize)
    , mMainSizer(nullptr)
    , mNavigationBrowser(nullptr)
    , mDividerLine(nullptr)
    , mContentPanel(nullptr)
    , mContentSizer(nullptr)
    , mIpc(nullptr)
    , mCurrentView(nullptr)
    , m_lifeTracker(std::make_shared<bool>(true))
{
    initUI();
}

HomeView::~HomeView()
{
    cleanupIPC();

    // Clean up homepage views
    for (auto& pair : mHomepageViews) {
        delete pair.second;
    }
    mHomepageViews.clear();
}

void HomeView::initUI()
{
    // Create main horizontal sizer
    mMainSizer = new wxBoxSizer(wxHORIZONTAL);

    // Create navigation webview (left side) with fixed width
    mNavigationBrowser = WebView::CreateWebView(this, "");

    if (!mNavigationBrowser) {
        wxMessageBox("Failed to create navigation webview", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // Remove WebView border completely
    mNavigationBrowser->SetWindowStyleFlag(wxNO_BORDER);

    // Create content panel (right side) - this will contain different HomepageViews
    mContentPanel = new wxPanel(this, wxID_ANY);
    mContentSizer = new wxBoxSizer(wxVERTICAL);
    mContentPanel->SetSizer(mContentSizer);

    // Add to main sizer with divider line
    mMainSizer->Add(mNavigationBrowser, 0, wxEXPAND | wxALL, 0);
    mMainSizer->Add(mContentPanel, 1, wxEXPAND, 0);

    // Set minimum size for navigation browser
    mNavigationBrowser->SetMinSize(wxSize(FromDIP(HOME_NAVIGATION_WIDTH_DIP), -1));
    SetSizer(mMainSizer);

    // Initialize IPC for navigation
    mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(mNavigationBrowser);
    setupIPCHandlers();

    // Delay creating homepage views until window is shown
    // This fixes macOS multi-display rendering issue where WKWebView fails to render
    // on extended displays if created before the window is fully displayed
    // Homepage views will be created by calling initializeNavigationWebView() after window is shown
    
    mNavigationBrowser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));
    
    // Hide WebView temporarily to avoid rendering issues on extended displays
    mNavigationBrowser->Hide();
}

void HomeView::msw_rescale()
{
    if (!mNavigationBrowser) {
        return;
    }

    mNavigationBrowser->SetMinSize(wxSize(FromDIP(HOME_NAVIGATION_WIDTH_DIP), -1));
    mNavigationBrowser->InvalidateBestSize();

    if (mContentPanel) {
        mContentPanel->Layout();
        mContentPanel->Refresh();
    }

    for (auto& pair : mHomepageViews) {
        if (pair.second) {
            pair.second->Layout();
            pair.second->Refresh();
        }
    }

    Layout();
    Refresh();
}

void HomeView::createHomepageViews()
{
    // Create Recent homepage view
    mHomepageViews["recent"] = new RecentHomepageView(mContentPanel);
    mContentSizer->Add(mHomepageViews["recent"], 1, wxEXPAND | wxALL, 0);
    mHomepageViews["recent"]->Show(false); // Initially hidden

    // Create Online Models homepage view
    // mHomepageViews["online-models"] = new OnlineModelsHomepageView(mContentPanel);
    // mContentSizer->Add(mHomepageViews["online-models"], 1, wxEXPAND | wxALL, 0);
    // mHomepageViews["online-models"]->Show(false); // Initially hidden

    // Initialize all views
    for (auto& pair : mHomepageViews) {
        pair.second->initialize();
    }
}

void HomeView::showPage(const wxString& pageName)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": showPage called with: %s") % pageName;

    // Hide all views first
    for (auto& pair : mHomepageViews) {
        pair.second->Show(false);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": hiding view: %s") % pair.first;
    }

    // Show the selected view
    auto it = mHomepageViews.find(pageName);
    if (it != mHomepageViews.end()) {
        mCurrentView = it->second;
        mCurrentView->Show(true);
        mContentSizer->Layout();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": showing view: %s") % pageName;
    } else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": view not found: %s") % pageName;
        if(pageName=="online-models"){
            // Lazy create online models view if not exists
            mHomepageViews["online-models"] = new OnlineModelsHomepageView(mContentPanel);
            mContentSizer->Add(mHomepageViews["online-models"], 1, wxEXPAND | wxALL, 0);
            mHomepageViews["online-models"]->initialize();
            mCurrentView = mHomepageViews["online-models"];
            mCurrentView->Show(true);
            mContentSizer->Layout();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": lazily created and showing view: %s") % pageName;
        }
    }
}

void HomeView::switchToPage(const wxString& pageName) { showPage(pageName); }

void HomeView::setupIPCHandlers()
{
    if (!mIpc)
        return;

    // Navigation handler - this is the key handler for switching pages
    mIpc->onRequest("navigateToPage", [this](const IPCRequest& request) { return handleNavigateToPage(request.params); });
    mIpc->onRequest("getUserInfo", [this](const IPCRequest& request) { return handleGetUserInfo(); });
    mIpc->onRequestAsync("logout", [this](const IPCRequest& request,
                                          std::function<void(const IPCResult&)> sendResponse) {

            try {
                auto result = handleLogout();
                sendResponse(result);
            } catch (const std::exception& e) {
                    sendResponse(IPCResult::error(std::string("Logout failed: ") + e.what()));
            } catch (...) {
                    sendResponse(IPCResult::error("Logout failed: Unknown error"));
            }
    });
    mIpc->onRequest("showLoginDialog", [this](const IPCRequest& request) { return handleShowLoginDialog(); });
    mIpc->onRequest("checkLoginStatus", [this](const IPCRequest& request) { return handleCheckLoginStatus(); });
    mIpc->onRequest("getRegion", [this](const IPCRequest& request) {
        std::shared_ptr<INetworkHelper> networkHelper = NetworkFactory::createNetworkHelper(PrintHostType::htElegooLink);
        std::string region = networkHelper ? networkHelper->getRegion() : "";
        return IPCResult::success(region);
    });
    mIpc->onRequest("ready", [this](const IPCRequest& request) { return handleReady(); });
}

void HomeView::cleanupIPC()
{
}

IPCResult HomeView::handleNavigateToPage(const nlohmann::json& data)
{
    std::string pageName   = data.value("page", "recent");
    wxString    wxPageName = wxString::FromUTF8(pageName);

    // Debug: Print the page name
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": navigating to page: %s") % wxPageName;

    // // Check if the page exists
    // auto it = mHomepageViews.find(wxPageName);
    // if (it == mHomepageViews.end()) {
    //     BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": page not found: %s") % wxPageName;
    //     return IPCResult::success();
    // }
    wxGetApp().CallAfter([this, wxPageName]() {
        switchToPage(wxPageName);
    });
    return IPCResult::success();
}

IPCResult HomeView::handleGetUserInfo()
{
    // Get user network info
    UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();   
    nlohmann::json data; 
    data = convertUserNetworkInfoToJson(userNetworkInfo);
    return IPCResult::success(data);
}

IPCResult HomeView::handleReady()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    mIsReady = true;
    // Send any pending user info with delay to ensure frontend is ready
    if(!mRefreshUserInfo.userId.empty() && mIpc) {
        nlohmann::json data = convertUserNetworkInfoToJson(mRefreshUserInfo);
        mIpc->sendEvent("onUserInfoUpdated", data, mIpc->generateRequestId());
        mRefreshUserInfo = UserNetworkInfo();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": sent pending user info to WebView";
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": ready";
    return IPCResult::success();
}

IPCResult HomeView::handleLogout()
{
    UserNetworkManager::getInstance()->logout();
    return IPCResult::success();
}

void HomeView::onWebViewLoaded(wxWebViewEvent& event)
{

}

void HomeView::onWebViewError(wxWebViewEvent& event)
{
    wxString error = event.GetString();
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": WebView Error: " << error.ToUTF8().data();
    // wxMessageBox("WebView Error: " + error, "Error", wxOK | wxICON_ERROR);
}

void HomeView::OnNavigationRequest(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    wxString url = evt.GetURL();
    url.Replace("\\", "/");
    if (url.StartsWith("File://") || url.StartsWith("file://")) {
        if (!url.Contains("/resources/web/") &&
            !url.Contains("/Resources/web/")) {
            wxString file = wxURL::Unescape(wxURL(evt.GetURL()).GetPath());
#ifdef _WIN32
            if (file.StartsWith('/')) {
                file = file.Mid(1);
            } else {
                file = "//" + file;
            }
#endif
            wxGetApp().plater()->load_files(wxArrayString{1, &file});
            evt.Veto();
            return;
        }
    }
}
void HomeView::OnNavigationComplete(wxWebViewEvent& evt){
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
}

void HomeView::OnNewWindowRequest(wxWebViewEvent& event)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << event.GetTarget().ToUTF8().data();
    wxString url = event.GetURL();
    url.Replace("\\", "/");
    
    if (url.StartsWith("File://") || url.StartsWith("file://")) {
        if (!url.Contains("/resources/web/") &&
            !url.Contains("/Resources/web/")) {
            wxString file = wxURL::Unescape(wxURL(event.GetURL()).GetPath());
#ifdef _WIN32
            if (file.StartsWith('/')) {
                file = file.Mid(1);
            } else {
                file = "//" + file;
            }
#endif
            wxGetApp().plater()->load_files(wxArrayString{1, &file});
            event.Veto();
            return;
        }
    }
}

void HomeView::sendRecentList(int images)
{
    if( mHomepageViews.find("recent") != mHomepageViews.end() ) {
        RecentHomepageView* recentView = dynamic_cast<RecentHomepageView*>(mHomepageViews["recent"]);
        if (recentView) {
            recentView->showRecentFiles(images);
        }
    }
}

IPCResult HomeView::handleShowLoginDialog()
{
    // Send event to MainFrame to show login dialog
    auto evt = new wxCommandEvent(EVT_USER_LOGIN);
    wxQueueEvent(wxGetApp().mainframe, evt);
    
    return IPCResult::success();
}

IPCResult HomeView::handleCheckLoginStatus()
{
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
        show_error(wxGetApp().mainframe, result.message);
        return IPCResult::error(result.message);
    }
    //don't need to re-login, return user info
    nlohmann::json  data = convertUserNetworkInfoToJson(userNetworkInfo);
    return IPCResult::success(data);
}
void HomeView::updateMode()
{
    // Update mode if needed
}

// void HomeView::RunScript(const wxString& javascript)
// {
//     if (m_browser) {
//         m_browser->RunScript(javascript);
//     }
// }

void HomeView::refreshUserInfo()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();
    if (mIpc && mIsReady) {
    // Send refresh signal to navigation webview via IPC
        nlohmann::json data = convertUserNetworkInfoToJson(userNetworkInfo);
        mIpc->sendEvent("onUserInfoUpdated", data, mIpc->generateRequestId());
        mRefreshUserInfo = UserNetworkInfo();
    } else {
        mRefreshUserInfo = userNetworkInfo;
    }
    //send event to homepage views
    for (auto& pair : mHomepageViews) {
        pair.second->onUserInfoUpdated(userNetworkInfo);
    }
}

void HomeView::onRegionChanged()
{
    //logout user
    UserNetworkManager::getInstance()->logout();
    if (mIpc && mIsReady) {
        mIpc->sendEvent("onRegionChanged", nlohmann::json::object(), mIpc->generateRequestId());
    }
    
    for (auto& pair : mHomepageViews) {
        pair.second->onRegionChanged();
    }
    
    refreshUserInfo();
}
void HomeView::onThemeChanged()
{
    //send event to homepage views
    for (auto& pair : mHomepageViews) {
        pair.second->onThemeChanged();
    }
}
void HomeView::initializeNavigationWebView()
{
    // Only initialize once
    bool expected = false;
    if (!mNavigationWebViewInitialized.compare_exchange_strong(expected, true)) {
        return;
    }
    
    if (!mNavigationBrowser) {
        return;
    }
    
    // Create homepage views first (they also contain WebViews that need to be created after window is shown)
    if (mHomepageViews.empty()) {
        createHomepageViews();
    }
    
    // Load navigation page (left side) - only navigation bar
    wxString navUrl = wxString::Format("file://%s/web/homepage4/navigation.html", from_u8(resources_dir()));
    mNavigationBrowser->LoadURL(navUrl);
    
    // Show WebView after loading URL
    mNavigationBrowser->Show();
    
    // Show default page
    showPage("recent");
    
    Layout();
}

}} // namespace Slic3r::GUI
