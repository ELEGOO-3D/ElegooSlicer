#include "ElegooPanel.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

ElegooPanel::ElegooPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos,
                         const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
    , mBrowser(nullptr)
    , mIpc(nullptr)
{
    setupCommonEvents();
}

ElegooPanel::~ElegooPanel()
{
    cleanupIPC();
}

void ElegooPanel::initWebView(const wxString& url)
{
    // Create webview
    mBrowser = WebView::CreateWebView(this, url);
    if (mBrowser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not create webview";
        return;
    }

    // Create IPC manager
    mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(mBrowser);
    setupIPCHandlers();

    // Enable dev tools if in developer mode
    mBrowser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));

    // Bind webview events
    mBrowser->Bind(wxEVT_WEBVIEW_LOADED, &ElegooPanel::onWebViewLoaded, this);
    mBrowser->Bind(wxEVT_WEBVIEW_ERROR, &ElegooPanel::onWebViewError, this);
    mBrowser->Bind(wxEVT_WEBVIEW_NAVIGATING, &ElegooPanel::OnNavigationRequest, this);
    mBrowser->Bind(wxEVT_WEBVIEW_NAVIGATED, &ElegooPanel::OnNavigationComplete, this);

    // Setup sizer and layout
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);
    sizer->Add(mBrowser, wxSizerFlags().Expand().Proportion(1));
}

void ElegooPanel::cleanupIPC()
{
    if (mBrowser && mIpc) {
        mBrowser->Unbind(wxEVT_WEBVIEW_LOADED, &ElegooPanel::onWebViewLoaded, this);
        mBrowser->Unbind(wxEVT_WEBVIEW_ERROR, &ElegooPanel::onWebViewError, this);
        mBrowser->Unbind(wxEVT_WEBVIEW_NAVIGATING, &ElegooPanel::OnNavigationRequest, this);
        mBrowser->Unbind(wxEVT_WEBVIEW_NAVIGATED, &ElegooPanel::OnNavigationComplete, this);
    }
    mIpc.reset();
}

void ElegooPanel::setupCommonEvents()
{
    // Common event setup can be added here if needed
}

void ElegooPanel::onWebViewLoaded(wxWebViewEvent& event)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": webview loaded, URL=" << event.GetURL();
    event.Skip();
}

void ElegooPanel::onWebViewError(wxWebViewEvent& event)
{
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": webview error, URL=" << event.GetURL()
                             << ", error=" << event.GetString();
    event.Skip();
}

void ElegooPanel::OnNavigationRequest(wxWebViewEvent& event)
{
    event.Skip();
}

void ElegooPanel::OnNavigationComplete(wxWebViewEvent& event)
{
    event.Skip();
}

}} // namespace Slic3r::GUI
