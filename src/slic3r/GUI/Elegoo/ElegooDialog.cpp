#include "ElegooDialog.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/event.h>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <thread>
#include <algorithm>

namespace Slic3r { namespace GUI {

ElegooDialog::ElegooDialog(wxWindow* parent, const wxString& title, const wxString& headline, long style)
    : MsgDialog(parent, title, headline, style)
    , m_browser(nullptr)
    , m_ipc(nullptr)
    , m_isLoading(false)
    , m_urlType(WebViewUrlType::Local)
{
    SetIcon(wxNullIcon);
    setupCommonEvents();
}

ElegooDialog::~ElegooDialog()
{
    cleanupIPC();
}

void ElegooDialog::initWebView(const wxString& url, WebViewUrlType urlType)
{
    m_urlType = urlType;
    
    // Create webview
    m_browser = WebView::CreateWebView(this, url);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not create webview";
        return;
    }

    // Create IPC manager
    m_ipc = std::make_unique<webviewIpc::WebviewIPCManager>(m_browser);
    
    // Setup common IPC handlers first
    setupCommonIPCHandlers();
    
    // Then setup subclass-specific handlers
    setupIPCHandlers();

    // Enable dev tools if in developer mode
    m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));

    // Bind webview events - bind to dialog (this) instead of browser
    // This ensures events are properly routed even if browser is destroyed
    Bind(wxEVT_WEBVIEW_LOADED, &ElegooDialog::onWebViewLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &ElegooDialog::onWebViewError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATING, &ElegooDialog::onNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &ElegooDialog::onNavigationComplete, this, m_browser->GetId());

    // Setup sizer and layout
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);
    sizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Hide logo if exists
    if (logo) {
        logo->Hide();
    }
}

void ElegooDialog::cleanupIPC()
{
    if (m_browser && m_ipc) {
        Unbind(wxEVT_WEBVIEW_LOADED, &ElegooDialog::onWebViewLoaded, this, m_browser->GetId());
        Unbind(wxEVT_WEBVIEW_ERROR, &ElegooDialog::onWebViewError, this, m_browser->GetId());
        Unbind(wxEVT_WEBVIEW_NAVIGATING, &ElegooDialog::onNavigationRequest, this, m_browser->GetId());
        Unbind(wxEVT_WEBVIEW_NAVIGATED, &ElegooDialog::onNavigationComplete, this, m_browser->GetId());
    }
    m_ipc.reset();
}

void ElegooDialog::setupCommonEvents()
{
    // Bind close event
    Bind(wxEVT_CLOSE_WINDOW, &ElegooDialog::onCloseWindow, this);

    // Bind ESC key hook to disable ESC key closing the dialog
    Bind(wxEVT_CHAR_HOOK, [](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE) {
            // Do nothing - disable ESC key
            return;
        }
        e.Skip();
    });
}


void ElegooDialog::onWebViewLoaded(wxWebViewEvent& event)
{
    m_isLoading = false;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": webview loaded, URL=" << event.GetURL();
    event.Skip();
}

void ElegooDialog::onNavigationRequest(wxWebViewEvent& event)
{
    m_isLoading = true;
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << event.GetTarget().ToUTF8().data();
    event.Skip();
}

void ElegooDialog::onNavigationComplete(wxWebViewEvent& event)
{
    m_isLoading = false;
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << event.GetTarget().ToUTF8().data();
    event.Skip();
}

void ElegooDialog::onWebViewError(wxWebViewEvent& event)
{
    m_isLoading = false;
    
    const char* errorType = "unknown error";
    switch (event.GetInt()) {
    case wxWEBVIEW_NAV_ERR_CONNECTION: errorType = "wxWEBVIEW_NAV_ERR_CONNECTION"; break;
    case wxWEBVIEW_NAV_ERR_CERTIFICATE: errorType = "wxWEBVIEW_NAV_ERR_CERTIFICATE"; break;
    case wxWEBVIEW_NAV_ERR_AUTH: errorType = "wxWEBVIEW_NAV_ERR_AUTH"; break;
    case wxWEBVIEW_NAV_ERR_SECURITY: errorType = "wxWEBVIEW_NAV_ERR_SECURITY"; break;
    case wxWEBVIEW_NAV_ERR_NOT_FOUND: errorType = "wxWEBVIEW_NAV_ERR_NOT_FOUND"; break;
    case wxWEBVIEW_NAV_ERR_REQUEST: errorType = "wxWEBVIEW_NAV_ERR_REQUEST"; break;
    case wxWEBVIEW_NAV_ERR_USER_CANCELLED: errorType = "wxWEBVIEW_NAV_ERR_USER_CANCELLED"; break;
    case wxWEBVIEW_NAV_ERR_OTHER: errorType = "wxWEBVIEW_NAV_ERR_OTHER"; break;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") 
                            % event.GetURL() % event.GetTarget() % errorType % event.GetString();

    std::string url = event.GetURL().ToStdString();
    
    // Skip if already on failed page
    if (url.find(FAILED_URL_SUFFIX) != std::string::npos) {
        event.Skip();
        return;
    }

    // Load failed page for network URLs
    if (m_urlType == WebViewUrlType::Network && m_browser) {
        loadFailedPage();
    }
    
    event.Skip();
}

void ElegooDialog::onCloseWindow(wxCloseEvent& event)
{
    EndModal(wxID_CANCEL);
}

void ElegooDialog::setupCommonIPCHandlers()
{
    if (!m_ipc) return;

    // Handle closeDialog - common IPC handler for closing the dialog
    m_ipc->onEvent("closeDialog", [this](const IPCEvent& event) {
        try {
            CallAfter([this]() {
                EndModal(wxID_CANCEL);
            });
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error in closeDialog: %s") % e.what();
        }
    });
}

void ElegooDialog::loadFailedPage()
{
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wxGetApp().CallAfter([this]() {
            if (m_browser) {
                auto path = resources_dir() + FAILED_URL_SUFFIX;
                #if WIN32
                    std::replace(path.begin(), path.end(), '\\', '/');
                #endif

                if(!boost::filesystem::exists(path)){
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed page not found: " << path;
                    return;
                }

                auto failedUrl = wxString::Format("file:///%s", from_u8(path));
                m_browser->LoadURL(failedUrl);
            }
        });
    }).detach();
}

}} // namespace Slic3r::GUI
