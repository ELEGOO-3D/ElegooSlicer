#ifndef HOMEPAGEVIEW_HPP
#define HOMEPAGEVIEW_HPP

#include <wx/wx.h>
#include <wx/webview.h>
#include <memory>
#include "slic3r/Utils/WebviewIPCManager.h"
#include "libslic3r/PrinterNetworkInfo.hpp"
namespace Slic3r { namespace GUI {

// Base class for different homepage views
class HomepageView : public wxPanel
{
public:
    HomepageView(wxWindow* parent, const wxString& name);
    virtual ~HomepageView() = default;
    
    virtual void initialize() {}
    virtual void updateMode() {}
    virtual void onUserInfoUpdated(const UserNetworkInfo& userNetworkInfo) {}
    virtual void onRegionChanged() {}
    virtual void onThemeChanged() {}

    const wxString& getName() const { return mName; }
    const bool isReady() const { return mIsReady; }

protected:
    wxString mName;
    std::atomic<bool> mIsReady = false;
};

// Recent files homepage view
class RecentHomepageView : public HomepageView
{
public:
    RecentHomepageView(wxWindow* parent);
    ~RecentHomepageView();
    
    void initialize() override;
    void updateMode() override;

    void showRecentFiles(int images);
private:
    void initUI();
    void setupIPCHandlers();
 
    // IPC handlers
    IPCResult handleGetRecentFiles(const nlohmann::json& data);
    IPCResult handleClearRecentFiles(const nlohmann::json& data);
    IPCResult handleOpenFile(const nlohmann::json& data);
    IPCResult handleCreateNewProject(const nlohmann::json& data);
    IPCResult handleOpenProject(const nlohmann::json& data);
    IPCResult handleOpenFileInExplorer(const nlohmann::json& data);
    IPCResult handleRemoveFromRecent(const nlohmann::json& data);
    
    // Event handlers
    void onWebViewLoaded(wxWebViewEvent& event);
    void onWebViewError(wxWebViewEvent& event);
    void OnNavigationRequest(wxWebViewEvent& event);
    void OnNavigationComplete(wxWebViewEvent& event);
private:
    wxWebView* mBrowser;
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;
    
    DECLARE_EVENT_TABLE()
};

// Online models homepage view - simple webview that loads remote URL
class OnlineModelsHomepageView : public HomepageView
{
public:
    OnlineModelsHomepageView(wxWindow* parent);
    ~OnlineModelsHomepageView();
    
    void initialize() override;
    void updateMode() override;
    void onUserInfoUpdated(const UserNetworkInfo& userNetworkInfo) override;
    void onRegionChanged() override;
    void onThemeChanged() override;
 
private:
    void initUI();
    void setupIPCHandlers();

    // Event handlers
    void onWebViewLoaded(wxWebViewEvent& event);
    void onWebViewError(wxWebViewEvent& event);
    void OnNavigationRequest(wxWebViewEvent& event);
    void OnNavigationComplete(wxWebViewEvent& event);
    IPCResult handleReady();

    void loadUrl(const wxString& url);
    void loadFailedPage();
private:
    wxWebView* mBrowser;
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;
    
    std::mutex mUserInfoMutex; // Mutex to protect user info
    UserNetworkInfo mRefreshUserInfo; // User info
    wxString mCurrentUrl; // Current URL to load
    bool mIsLoading = false;
    DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif // HOMEPAGEVIEW_HPP
