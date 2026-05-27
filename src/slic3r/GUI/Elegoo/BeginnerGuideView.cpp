#include "BeginnerGuideView.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

namespace Slic3r { namespace GUI {

BeginnerGuideView::BeginnerGuideView(wxWindow* parent)
    : ElegooDialog(parent, _L("Beginner Guide"), _L(""), 0)
{
    // Build local URL with language and dev mode parameters
    wxString targetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/beginner-guide/index.html").make_preferred().string());
    targetUrl = "file://" + targetUrl;
    wxString strlang = wxGetApp().current_language_code_safe();
    if (!strlang.IsEmpty()) {
        targetUrl = wxString::Format("%s?lang=%s", targetUrl, strlang);
    }
    if (wxGetApp().app_config->get_bool("developer_mode")) {
        targetUrl = targetUrl + "&dev=true";
    }

    // Initialize WebView with local URL
    initWebView(targetUrl, WebViewUrlType::Local);
    
    // Set dialog size
    wxSize pSize = FromDIP(wxSize(1200, 740));
    SetSize(pSize);
    CenterOnParent();
}
int BeginnerGuideView::ShowModal()
{
    // Refresh the web view when shown
    if (getBrowser()) {
        getBrowser()->Refresh();
    }
    return MsgDialog::ShowModal();
}

}} // namespace Slic3r::GUI
