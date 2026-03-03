#include "PrinterMmsSyncView.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/Elegoo/PrinterManager.hpp"
#include "slic3r/Utils/Elegoo/PrinterMmsManager.hpp"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>


namespace Slic3r { namespace GUI {

PrinterMmsSyncView::PrinterMmsSyncView(wxWindow* parent) : MsgDialog(static_cast<wxWindow*>(wxGetApp().mainframe), _L("Synchronize filament list"), _L(""), 0)
{
    const AppConfig* app_config = wxGetApp().app_config;

    auto preset_bundle = wxGetApp().preset_bundle;
    auto model_id      = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);

    // Bind close event to handle async operations
    Bind(wxEVT_CLOSE_WINDOW, &PrinterMmsSyncView::OnCloseWindow, this);
    
    // Bind ESC key hook to disable ESC key closing the dialog
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE) {
            // Do nothing - disable ESC key closing the dialog
            return;
        }
        e.Skip();
    });

    SetIcon(wxNullIcon);
    // DestroyChildren();
    mBrowser = WebView::CreateWebView(this, "");
    if (mBrowser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not init m_browser";
        return;
    }

    mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(mBrowser);
    setupIPCHandlers();
    // mBrowser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterMmsSyncView::onScriptMessage, this);
    mBrowser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));
    wxString TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/printer/filament_sync/index.html").make_preferred().string());
    TargetUrl          = "file://" + TargetUrl;
    wxString strlang = wxGetApp().current_language_code_safe();
    if (strlang != "")
        TargetUrl = wxString::Format("%s?lang=%s", TargetUrl, strlang);
    if(wxGetApp().app_config->get_bool("developer_mode")){
            TargetUrl = TargetUrl + "&dev=true";
    }

    mBrowser->LoadURL(TargetUrl);
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(topsizer);
    topsizer->Add(mBrowser, wxSizerFlags().Expand().Proportion(1));
    wxSize pSize = FromDIP(wxSize(800, 700));
    SetSize(pSize);
    CenterOnParent();

    if (logo) {
        logo->Hide();
    }
}

PrinterMmsSyncView::~PrinterMmsSyncView() {
}

void PrinterMmsSyncView::setupIPCHandlers()
{
    if (!mIpc) return;

    // Handle getPrinterList
    mIpc->onRequest("getPrinterList", [this](const IPCRequest& request) {
        try {
            return getPrinterList();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error in getPrinterList: %s") % e.what();
            return IPCResult::error("Failed to get printer list");
        }
    });

    // Handle getPrinterFilamentInfo (async due to potentially time-consuming operations)
    mIpc->onRequestAsync("getPrinterFilamentInfo", [this](const IPCRequest& request,
                                                          std::function<void(const IPCResult&)> sendResponse) {
        nlohmann::json params = request.params;
        try {
            IPCResult response = this->getPrinterFilamentInfo(params);
            sendResponse(response);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error in getPrinterFilamentInfo: %s") % e.what();
            sendResponse(IPCResult::error(std::string("Failed to get printer filament info: ") + e.what()));  
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unknown error in getPrinterFilamentInfo";
            sendResponse(IPCResult::error("Failed to get printer filament info: Unknown error"));
        }
    });

    // Handle syncMmsFilament
    mIpc->onEvent("syncMmsFilament", [this](const IPCEvent& event) {
        try {
            auto data = event.data;
            CallAfter([this,data]() {
                syncMmsFilament(data);
                EndModal(wxID_OK);
            });
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error in syncMmsFilament: %s") % e.what();
        }
    });

    // Handle closeDialog
    mIpc->onEvent("closeDialog", [this](const IPCEvent& event) {
        try {
            CallAfter([this]() {
                EndModal(wxID_CANCEL);
            });
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error in closeDialog: %s") % e.what();
        }
    });
}

void PrinterMmsSyncView::EndModal(int ret)
{
    MsgDialog::EndModal(ret);
}

IPCResult PrinterMmsSyncView::getPrinterList()
{
    IPCResult result;
    std::string selectedPrinterId = wxGetApp().app_config->get("recent", CONFIG_KEY_SELECTED_PRINTER_ID);
    auto printerList = PrinterManager::getInstance()->getPrinterList();
    nlohmann::json printerArray = json::array();
    for (auto& printer : printerList) {
        if(!printer.systemCapabilities.supportsMultiFilament) {
            continue;
        }
        nlohmann::json printerObj = nlohmann::json::object();
        printerObj = convertPrinterNetworkInfoToJson(printer);
        if (selectedPrinterId.empty() || printer.printerId != selectedPrinterId) {
            printerObj["selected"] = false;
        } else {
            printerObj["selected"] = true;
        }
        boost::filesystem::path resources_path(Slic3r::resources_dir());
        std::string img_path = resources_path.string() + "/profiles/" + printer.vendor + "/" + printer.printerModel + "_cover.png";
        printerObj["printerImg"] = PrinterManager::imageFileToBase64DataURI(img_path);
        printerArray.push_back(printerObj);
    }

    std::string printerModel = "";
    auto cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    auto printerModelValue = cfg.option<ConfigOptionString>("printer_model");
    if (printerModelValue) {
        printerModel = printerModelValue->value;
    }
    PrinterNetworkInfo selectedPrinter = PrinterManager::getInstance()->getSelectedPrinter(printerModel, selectedPrinterId);
    for(auto& printer : printerArray) {
        if(printer["printerId"].get<std::string>() == selectedPrinter.printerId) {
            printer["selected"] = true;
        } else {
            printer["selected"] = false;
        }
    }
    result.data = printerArray;
    result.code = 0;
    result.message = getErrorMessage(PrinterNetworkErrorCode::SUCCESS);
    return result;
}

IPCResult PrinterMmsSyncView::getPrinterFilamentInfo(const nlohmann::json& params)
{
    IPCResult result;
    nlohmann::json printerFilamentInfo = nlohmann::json::object();
    std::string printerId = params["printerId"];
    auto mmsGroupResult = PrinterMmsManager::getInstance()->getPrinterMmsInfo(printerId);
    if(!mmsGroupResult.isSuccess() || !mmsGroupResult.hasData()) {
        result.code = static_cast<int>(mmsGroupResult.code);
        result.message = mmsGroupResult.message;
        return result;
    }
    if(!mmsGroupResult.data.value().connected) {
        result.code = static_cast<int>(PrinterNetworkErrorCode::PRINTER_MMS_NOT_CONNECTED);
        std::string mmsSystemName = mmsGroupResult.data.value().mmsSystemName;
        std::string errorMessage = (boost::format(_u8L("%1% connection failed. Please check and try again.")) % mmsSystemName).str();
        result.message = errorMessage;
        return result;
    }
    PrinterMmsGroup mmsGroup = mmsGroupResult.data.value();
    nlohmann::json mmsInfo = convertPrinterMmsGroupToJson(mmsGroup);
    printerFilamentInfo["mmsInfo"] = mmsInfo;

    auto  preset_bundle = wxGetApp().preset_bundle;
    std::vector<PrintFilamentMmsMapping> printFilamentList;
    nlohmann::json printFilamentArray     = json::array();

    std::map<std::string, Preset*> nameToPreset;
    for (int i = 0; i < preset_bundle->filaments.size(); ++i) {
        Preset* preset             = &preset_bundle->filaments.preset(i);
        nameToPreset[preset->name] = preset;
    }

    std::vector<PrintFilamentMmsMapping> allFilamentList;
    for (const auto& filamentName : preset_bundle->filament_presets) {
        auto it = nameToPreset.find(filamentName);
        if (it != nameToPreset.end()) {
            PrintFilamentMmsMapping filament;
            Preset*                 preset = it->second;
            std::string             displayFilamentType;
            std::string             filamentType = preset->config.get_filament_type(displayFilamentType);
            std::string displayFilamentName = filamentName;
            size_t      pos                 = displayFilamentName.find('@');
            if (pos != std::string::npos) {
                displayFilamentName = displayFilamentName.substr(0, pos);
            }
            filament.filamentType    = filamentType;
            filament.filamentId      = preset->filament_id;
            filament.filamentName    = displayFilamentName;
            allFilamentList.push_back(filament);
        }
    }
    auto extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_used_extruders();
    for (auto i = 0; i < extruders.size(); i++) {
        int extruderIdx = extruders[i] - 1;
        if (extruderIdx < 0 || extruderIdx >= (int) allFilamentList.size())
            continue;
        auto info = allFilamentList[extruderIdx];
        printFilamentList.push_back(info);
    }
    PrinterNetworkInfo printerNetworkInfo = PrinterManager::getInstance()->getPrinterNetworkInfo(printerId);
    if(!printerNetworkInfo.printerId.empty()) {
        PrinterMmsManager::getInstance()->getFilamentMmsMapping(printerNetworkInfo, printFilamentList, mmsGroup);
        for (auto& printFilament : printFilamentList) {
            printFilamentArray.push_back(convertPrintFilamentMmsMappingToJson(printFilament));
        }
    }

    auto        cfg               = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    std::string printerModel      = "";
    auto        printerModelValue = cfg.option<ConfigOptionString>("printer_model");
    if (printerModelValue) {
        printerModel = printerModelValue->value;
    }
    printerFilamentInfo["currentProjectPrinterModel"] = printerModel;
    printerFilamentInfo["printFilamentList"] = printFilamentArray;
    result.data = printerFilamentInfo;
    result.code = 0;
    result.message = getErrorMessage(PrinterNetworkErrorCode::SUCCESS);
    return result;
}

IPCResult PrinterMmsSyncView::syncMmsFilament(const nlohmann::json& params)
{
    IPCResult result;
    nlohmann::json mmsInfo = params["mmsInfo"];
    nlohmann::json printFilamentList = params["printFilamentList"];
    nlohmann::json printer = params["printer"];

    mMmsGroup  = convertJsonToPrinterMmsGroup(mmsInfo);

    std::string selectedPrinterId = printer["printerId"].get<std::string>();
    if(!selectedPrinterId.empty()) {
        wxGetApp().app_config->set("recent", CONFIG_KEY_SELECTED_PRINTER_ID, selectedPrinterId);
    }
    result.code = 0;
    result.message = getErrorMessage(PrinterNetworkErrorCode::SUCCESS);
    return result;
}

PrinterMmsGroup PrinterMmsSyncView::getSyncedMmsGroup()
{
    return mMmsGroup;
}

void PrinterMmsSyncView::onShow()
{
    // Refresh the web view when shown
    if (mBrowser) {
        mBrowser->Refresh();
    }
}

int PrinterMmsSyncView::ShowModal()
{
    onShow();
    return MsgDialog::ShowModal();
}

void PrinterMmsSyncView::OnCloseWindow(wxCloseEvent& event)
{
    // // If async operation is in progress, prevent closing
    // if (m_asyncOperationInProgress && !m_isDestroying) {
    //     // Show a message to user that operation is in progress
    //     // wxMessageBox(_L("Filament info retrieval is in progress. Please wait..."), 
    //     //              _L("Operation in Progress"), wxOK | wxICON_INFORMATION);
    //     event.Veto(); // Prevent the window from closing
    //     return;
    // }
    
    // Allow normal close behavior
    event.Skip();
}

}} // namespace Slic3r::GUI
