#include "DownloadProgressDialog.hpp"

#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/statbmp.h>
#include <wx/scrolwin.h>
#include <wx/clipbrd.h>
#include <wx/checkbox.h>
#include <wx/html/htmlwin.h>

#include <boost/algorithm/string/replace.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
//#include "ConfigWizard.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "GUI_App.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"

#include "Widgets/HyperLink.hpp" // ORCA

#define DESIGN_INPUT_SIZE wxSize(FromDIP(100), -1)

namespace Slic3r {
namespace GUI {



DownloadProgressDialog::DownloadProgressDialog(wxString                                       title, std::function<bool(ButtonAction, int)> user_action_callback,
                                               int                                            download_id)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_user_action_callback(user_action_callback)
    , m_download_id(download_id)
{

    wxString download_failed_msg = _L("Failed to download the plug-in. Please check your firewall settings and VPN software and retry.");
    wxString install_failed_msg = _L("Failed to install the plug-in. The plug-in file may be in use. Please restart ElegooSlicer and try again. Also check whether it is blocked or deleted by anti-virus software.");

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);

    m_simplebook_status = new wxSimplebook(this);
    m_simplebook_status->SetSize(wxSize(FromDIP(300), FromDIP(50)));
    m_simplebook_status->SetMinSize(wxSize(FromDIP(300), FromDIP(50)));
    m_simplebook_status->SetMaxSize(wxSize(FromDIP(300), FromDIP(50)));

    m_status_bar = std::make_shared<BBLStatusBarSend>(m_simplebook_status);
    m_status_bar->set_download_user_action([this](ButtonAction action) -> void {
        if (m_close)
            return;
        if (ButtonActionCanceled == action) {
            Close();          
        } else if (ButtonActionPaused == action) {
            m_user_action_callback(ButtonActionPaused, m_download_id);
        } else if(ButtonActionContinued == action) {
            m_user_action_callback(ButtonActionContinued, m_download_id);
        } else if(ButtonActionOpenedFolder == action) {
            m_user_action_callback(ButtonActionOpenedFolder, m_download_id);
        }      
    });
    //m_status_bar->set_download_user_action(std::function() )
    m_panel_download = m_status_bar->get_panel();
    m_panel_download->SetSize(wxSize(FromDIP(300), FromDIP(50)));
    m_panel_download->SetMinSize(wxSize(FromDIP(300), FromDIP(50)));
    m_panel_download->SetMaxSize(wxSize(FromDIP(300), FromDIP(50)));

    m_simplebook_status->AddPage(m_status_bar->get_panel(), wxEmptyString, true);

    m_sizer_main->Add(m_simplebook_status, 0, wxALL, FromDIP(20));
    m_sizer_main->Add(0, 0, 1, wxBOTTOM, 10);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    CentreOnParent();

    Bind(wxEVT_CLOSE_WINDOW, &DownloadProgressDialog::on_close, this);
    wxGetApp().UpdateDlgDarkUI(this);
}

bool DownloadProgressDialog::Show(bool show)
{ 
    return DPIDialog::Show(show);
}

void DownloadProgressDialog::on_close(wxCloseEvent& event)
{
    if (m_close)
        return;
    m_close = true;
    m_user_action_callback(ButtonActionCanceled, m_download_id);
    event.Skip();
}

DownloadProgressDialog::~DownloadProgressDialog() {}

void DownloadProgressDialog::on_dpi_changed(const wxRect &suggested_rect) {}

void DownloadProgressDialog::download_progress(int progress) {
    m_status_bar->set_progress(progress); 
}

void DownloadProgressDialog::download_error(const wxString& error_message, const wxString& description)
{
    m_status_bar->show_error_info(error_message, 0, description, "");
}
void DownloadProgressDialog::download_paused() {

}
void DownloadProgressDialog::download_canceled()
{
    if (m_close)
        return;
    m_close = true;
    Close();
}


}} // namespace Slic3r::GUI
