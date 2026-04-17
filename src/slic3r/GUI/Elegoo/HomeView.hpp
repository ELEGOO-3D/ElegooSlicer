#ifndef HOMEVIEW_HPP
#define HOMEVIEW_HPP

#include <wx/wx.h>
#include <wx/statline.h>
#include <wx/webview.h>
#include <memory>
#include <map>
#include <atomic>
#include <mutex>
#include <optional>
#include <nlohmann/json.hpp>
#include "slic3r/Utils/WebviewIPCManager.h"
#include "libslic3r/PrinterNetworkInfo.hpp"

namespace Slic3r { namespace GUI {

// Forward declarations
class HomepageView;

class HomeView : public wxPanel
{
public:
    HomeView(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = "Home");
    ~HomeView();

    void sendRecentList(int images);
    void msw_rescale();
    void updateMode();
    void switchToPage(const wxString& pageName);
    void refreshUserInfo();
    void onRegionChanged();
    void onThemeChanged();
    
    // Initialize navigation WebView after window is shown (fixes macOS multi-display rendering issue)
    void initializeNavigationWebView();
    
private:
    void initUI();
    void setupIPCHandlers();
    void cleanupIPC();
    void createHomepageViews();
    void showPage(const wxString& pageName);
    
    // IPC handlers
    IPCResult handleGetUserInfo();
    IPCResult handleNavigateToPage(const nlohmann::json& data);
    IPCResult handleShowLoginDialog();
    IPCResult handleCheckLoginStatus();
    IPCResult handleReady();
    
    // Async operations
    IPCResult handleLogout();
    
    // Event handlers
    void onWebViewLoaded(wxWebViewEvent& event);
    void onWebViewError(wxWebViewEvent& event);
    void OnNavigationRequest(wxWebViewEvent& event);
    void OnNavigationComplete(wxWebViewEvent& event);
    void OnNewWindowRequest(wxWebViewEvent& event);
private:
    // UI Components
    wxBoxSizer* mMainSizer;
    wxWebView* mNavigationBrowser;  // Left navigation webview
    wxStaticLine* mDividerLine;     // Vertical divider line
    wxPanel* mContentPanel;         // Right content panel
    wxBoxSizer* mContentSizer;     // Sizer for content panel
    // IPC
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;
    
    // Homepage views
    std::map<wxString, HomepageView*> mHomepageViews;
    HomepageView* mCurrentView;
    

    std::atomic<bool> mIsReady{false};
    std::atomic<bool> mNavigationWebViewInitialized{false};
    std::mutex mUserInfoMutex; // Mutex to protect user info
    UserNetworkInfo mRefreshUserInfo; // User info
    std::shared_ptr<bool> m_lifeTracker; // Lifetime tracker for async operations
    DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif // HOMEVIEW_HPP
