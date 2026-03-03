#pragma once

#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/Utils/WebviewIPCManager.h"

#include <wx/panel.h>
#include <wx/webview.h>
#include <memory>

namespace Slic3r { namespace GUI {

// Base panel class for Elegoo panels that use WebView and IPC
class ElegooPanel : public wxPanel
{
public:
    ElegooPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL | wxNO_BORDER);
    virtual ~ElegooPanel();

    // Initialize WebView and IPC manager
    // Subclasses should call this in their constructor or initUI method
    void initWebView(const wxString& url = wxEmptyString);
    
    // Setup IPC handlers - override this in subclasses
    virtual void setupIPCHandlers() {}
    
    // Cleanup IPC manager - called automatically in destructor
    void cleanupIPC();

    // Access to WebView and IPC manager
    wxWebView* getBrowser() const { return mBrowser; }
    webviewIpc::WebviewIPCManager* getIpc() const { return mIpc.get(); }

protected:
    // Override these for custom behavior
    virtual void onWebViewLoaded(wxWebViewEvent& event);
    virtual void onWebViewError(wxWebViewEvent& event);
    virtual void OnNavigationRequest(wxWebViewEvent& event);
    virtual void OnNavigationComplete(wxWebViewEvent& event);

    wxWebView* mBrowser;
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;

private:
    void setupCommonEvents();
};

}} // namespace Slic3r::GUI
