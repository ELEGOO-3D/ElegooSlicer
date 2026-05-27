#pragma once


#include <boost/filesystem/path.hpp>

#include <wx/dialog.h>
#include <wx/webview.h>
#include <wx/colour.h>
#include <wx/string.h>
#include <wx/event.h>
#include <wx/dialog.h>

#include <nlohmann/json.hpp>
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"
#include <slic3r/Utils/WebviewIPCManager.h>

#if wxUSE_WEBVIEW_IE
#include "wx/msw/webview_ie.h"
#endif
#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

namespace webviewIpc {
    class WebviewIPCManager;
}

namespace Slic3r { namespace GUI {
class PrintSendDialogEx : public GUI::MsgDialog
{
public:
    PrintSendDialogEx(Plater* plater, int printPlateIdx, const boost::filesystem::path& path);

    ~PrintSendDialogEx();

    void init() ;
    boost::filesystem::path filename() const {
        return into_path(mModelName);
    }

    virtual void EndModal(int ret) override;

    std::map<std::string, std::string> getExtendedInfo() const ;
    PrintHostPostUploadAction getPostAction() const;
    bool getSwitchToDeviceTab() const;

protected:
    void OnCloseWindow(wxCloseEvent& event);

    BedType getCurrentBedType() const;
private:
    void setupIPCHandlers();
    IPCResult getPrinterList();
    IPCResult preparePrintTask(const std::string &printerId);
    IPCResult getPrinterMmsInfo(const std::string &printerId);
    IPCResult onPrint(const nlohmann::json &printInfo);
    void onCancel();
    std::string getCurrentProjectName(); 
    BedType appBedType() const;
    void    refresh();

    wxWebView* mBrowser;
    std::unique_ptr<webviewIpc::WebviewIPCManager> mIpc;
    Plater*  mPlater{ nullptr };
    int mPrintPlateIdx;

    bool    mTimeLapse{false};
    bool    mHeatedBedLeveling;
    BedType mBedType;
    bool    mAutoRefill;
    PrintHostPostUploadAction mPostUploadAction;
    bool mSwitchToDeviceTab;
    wxString mModelName;
    boost::filesystem::path mPath;
    std::string mSelectedPrinterId;
    std::string mProjectName;
    std::vector<PrintFilamentMmsMapping> mPrintFilamentList;
    bool mHasMms;
    PrinterMmsGroup mMmsGroup;
   
};
}} // namespace Slic3r::GUI 
