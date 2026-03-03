#pragma once

#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/Utils/WebviewIPCManager.h"

#include <wx/webview.h>
#include <memory>
#include <atomic>

namespace Slic3r { namespace GUI {

// URL type for WebView
enum class WebViewUrlType {
    Local,    // Local file (file://)
    Network   // Network URL (http:// or https://)
};

// Base dialog class for Elegoo dialogs that use WebView and IPC
class ElegooDialog : public MsgDialog
{
public:
    ElegooDialog(wxWindow* parent, const wxString& title, const wxString& headline, long style = 0);
    virtual ~ElegooDialog();

    // Initialize WebView and IPC manager
    // @param url: URL to load
    // @param urlType: URL type (default: Local)
    void initWebView(const wxString& url = wxEmptyString, WebViewUrlType urlType = WebViewUrlType::Local);
    
    // Setup IPC handlers - override this in subclasses to add custom handlers
    // Common handlers (like closeDialog) are already set up in base class
    virtual void setupIPCHandlers() {}
    
    // Cleanup IPC manager - called automatically in destructor
    void cleanupIPC();

    // Access to WebView and IPC manager
    wxWebView* getBrowser() const { return m_browser; }
    webviewIpc::WebviewIPCManager* getIpc() const { return m_ipc.get(); }
    
    // Check if WebView is currently loading
    bool isLoading() const { return m_isLoading.load(); }

protected:
    // Override these for custom behavior
    virtual void onWebViewLoaded(wxWebViewEvent& event);
    virtual void onWebViewError(wxWebViewEvent& event);
    virtual void onCloseWindow(wxCloseEvent& event);
    virtual void onNavigationRequest(wxWebViewEvent& event);
    virtual void onNavigationComplete(wxWebViewEvent& event);

private:
    wxWebView* m_browser;
    std::unique_ptr<webviewIpc::WebviewIPCManager> m_ipc;
    void setupCommonEvents();
    void setupCommonIPCHandlers();
    void loadFailedPage();
    
    std::atomic<bool> m_isLoading;
    WebViewUrlType m_urlType;
    
    static constexpr const char* FAILED_URL_SUFFIX = "/web/error-page/connection-failed.html";
};

}} // namespace Slic3r::GUI
