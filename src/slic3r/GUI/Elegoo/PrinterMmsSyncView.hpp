#pragma once
#include <map>
#include <functional>
#include <memory>
#include <atomic>
#include <wx/webview.h>
#include <wx/aui/aui.h>
#include <wx/colour.h>
#include <nlohmann/json.hpp>
#include "slic3r/GUI/MsgDialog.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"
#include <slic3r/Utils/WebviewIPCManager.h>
#if wxUSE_WEBVIEW_IE
#include "wx/msw/webview_ie.h"
#endif
#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif


namespace Slic3r { namespace GUI {

class PrinterMmsSyncView : public GUI::MsgDialog
{
public:
    PrinterMmsSyncView(wxWindow *parent);
    virtual ~PrinterMmsSyncView();
    virtual int ShowModal() override;
    virtual void EndModal(int ret) override;
    
    PrinterMmsGroup getSyncedMmsGroup();

private:
    void setupIPCHandlers();
    void OnCloseWindow(wxCloseEvent& event);
    IPCResult getPrinterList();
    IPCResult getPrinterFilamentInfo(const nlohmann::json& params);
    IPCResult syncMmsFilament(const nlohmann::json& params);
    void onShow();

    wxWebView* mBrowser;   
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;
    PrinterMmsGroup mMmsGroup;
};
}} // namespace Slic3r::GUI 
