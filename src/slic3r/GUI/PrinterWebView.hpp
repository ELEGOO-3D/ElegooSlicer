#ifndef slic3r_PrinterWebView_hpp_
#define slic3r_PrinterWebView_hpp_

#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include <wx/webview.h>
#include <wx/string.h>
#include <thread>
#include <mutex>
#include <atomic>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>
#include "slic3r/Utils/WebviewIPCManager.h"
#include <memory>
namespace webviewIpc {
class WebviewIPCManager;
}

namespace Slic3r { namespace GUI {

class PrinterWebView : public wxPanel
{
public:
    PrinterWebView(wxWindow* parent);
    virtual ~PrinterWebView();

    void load_url(const wxString& url, const wxString& apikey = "");
    void UpdateState();
    void OnClose(const wxCloseEvent& evt);
    void OnError(const wxWebViewEvent& evt);
    void OnLoaded(const wxWebViewEvent& evt);
    void reload();
    void update_mode();


    void onRtcTokenChanged(const nlohmann::json& data);
    void onRtmMessage(const nlohmann::json& data);
    void onConnectionStatus(const nlohmann::json& data);
    void onPrinterEventRaw(const nlohmann::json& data);

    bool Show(bool show = true) override;
private:
    void SendAPIKey();
    void OnScriptMessage(const wxWebViewEvent& event);
    void OnNavgating(wxWebViewEvent& event);
    void OnNavgated(const wxWebViewEvent& event);
    void loadConnectingPage();
    void loadFailedPage();
    void loadInputUrl();
    void loadUrl(const wxString& url);
    void runScript(const wxString& javascript);
    void setupIPCHandlers();

    wxWebView* m_browser;
    long       m_zoomFactor;
    wxString   m_apikey;
    bool       m_apikey_sent;
    // DECLARE_EVENT_TABLE()

    wxString m_url;
    wxString m_connectiongUrl;
    wxString m_failedUrl;
    enum PWLoadState { CONNECTING_LOADING = 0, CONNECTING_LOADED, URL_LOADING, URL_LOADED, FAILED_LOADING, FAILED_LOADED };
    // 0 is load connecting page
    // 1 is load url
    // 2 is load failed page
    PWLoadState m_loadState = PWLoadState::CONNECTING_LOADING;

    // Upload thread management
    std::atomic<bool> m_uploadInProgress;
    std::atomic<bool> m_shouldStop;
private:
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;
    
    wxString m_url_deferred;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
