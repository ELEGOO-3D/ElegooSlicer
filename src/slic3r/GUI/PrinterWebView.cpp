#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/PrinterWebView.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"
#include "libslic3r/Utils.hpp"

#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/webview.h>
#include "slic3r/GUI/PhysicalPrinterDialog.hpp"
#include "slic3r/Utils/Elegoo/PrinterManager.hpp"
#include "slic3r/Utils/Elegoo/UserNetworkManager.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <slic3r/Utils/WebviewIPCManager.h>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

#define CONNECTIONG_URL_SUFFIX "/web/orca/connecting.html"
#define FAILED_URL_SUFFIX "/web/orca/connection-failed.html"
namespace Slic3r { namespace GUI {
PrinterWebView::PrinterWebView(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    m_uploadInProgress = false;
    m_shouldStop       = false;
    
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not init m_browser";
        return;
    }
    this->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    m_browser->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    m_browser->SetOwnBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(m_browser);
    setupIPCHandlers();
    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &PrinterWebView::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &PrinterWebView::OnLoaded, this);
    m_browser->Bind(wxEVT_WEBVIEW_NAVIGATING, &PrinterWebView::OnNavgating, this);
    m_browser->Bind(wxEVT_WEBVIEW_NAVIGATED, &PrinterWebView::OnNavgated, this);
    SetSizer(topsizer);
    // m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this);

    auto _dir = resources_dir();
    // replace all "\\" with "/"
    std::replace(_dir.begin(), _dir.end(), '\\', '/');

    m_connectiongUrl = wxString::Format("file:///%s" + wxString(CONNECTIONG_URL_SUFFIX), from_u8(_dir));
    m_failedUrl      = wxString::Format("file:///%s" + wxString(FAILED_URL_SUFFIX), from_u8(_dir));

    auto injectJsPath = boost::format("%1%/web/orca/inject.js") % _dir;
    // read file
    std::string   injectJs;
    std::ifstream injectJsFile(std::filesystem::u8path(injectJsPath.str()));
    if (injectJsFile.is_open()) {
        std::string line;
        while (std::getline(injectJsFile, line)) {
            injectJs += line;
        }
        injectJsFile.close();
    } else {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not open inject.js";
    }
    //
    // #ifdef WIN32
    //    injectJs = "let isWin=true;\n" + injectJs;
    // #endif // WIN32

    // Add inject.js to the webview
    bool ret = m_browser->AddUserScript(injectJs);
    if (!ret) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not add user script";
    }

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
    update_mode();

    // Log backend information
    /* m_browser->GetUserAgent() may lead crash
    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("Backend: %s Version: %s", m_browser->GetClassInfo()->GetClassName(),
            wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("User Agent: %s", m_browser->GetUserAgent());
    }
    */

    // Zoom
    m_zoomFactor = 100;

    // Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);
}

PrinterWebView::~PrinterWebView()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);

    // stop upload thread
    m_shouldStop = true;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void PrinterWebView::load_url(const wxString& url, const wxString& apikey)
{
    //    this->Show();
    //    this->Raise();
    if (m_browser == nullptr)
        return;
    m_apikey      = apikey;
    m_apikey_sent = false;
    m_url         = url;
    loadConnectingPage();
    // m_browser->SetFocus();
    UpdateState();
}
void PrinterWebView::OnNavgating(wxWebViewEvent& event)
{
    auto url = event.GetURL();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": url %1%") % url;

    // Because the space in the browser url is escaped as %20, so when comparing strings,
    // the full path cannot be compared, use SUFFIX for comparison
    if ((m_loadState != PWLoadState::CONNECTING_LOADING && url.Contains(CONNECTIONG_URL_SUFFIX)) ||
        (m_loadState != PWLoadState::FAILED_LOADING && url.Contains(FAILED_URL_SUFFIX))) {
        m_loadState = PWLoadState::CONNECTING_LOADING;
        event.Veto();
        loadConnectingPage();
    }
}
void PrinterWebView::OnNavgated(const wxWebViewEvent& event) {}
void PrinterWebView::reload() { m_browser->Reload(); }

void PrinterWebView::update_mode() { m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode")); }

/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void PrinterWebView::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void PrinterWebView::OnClose(const wxCloseEvent& evt) {  }

void PrinterWebView::SendAPIKey()
{
    if (m_apikey_sent || m_apikey.IsEmpty())
        return;
    m_apikey_sent   = true;
    wxString script = wxString::Format(R"(
    // Check if window.fetch exists before overriding
    if (window.fetch) {
        const originalFetch = window.fetch;
        window.fetch = function(input, init = {}) {
            init.headers = init.headers || {};
            init.headers['X-API-Key'] = '%s';
            return originalFetch(input, init);
        };
    }
)",
                                       m_apikey);
    // m_browser->RemoveAllUserScripts();

    m_browser->AddUserScript(script);
    m_browser->Reload();
}

void PrinterWebView::OnError(const wxWebViewEvent& evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
    case wxWEBVIEW_NAV_ERR_CONNECTION: e = "wxWEBVIEW_NAV_ERR_CONNECTION"; break;
    case wxWEBVIEW_NAV_ERR_CERTIFICATE: e = "wxWEBVIEW_NAV_ERR_CERTIFICATE"; break;
    case wxWEBVIEW_NAV_ERR_AUTH: e = "wxWEBVIEW_NAV_ERR_AUTH"; break;
    case wxWEBVIEW_NAV_ERR_SECURITY: e = "wxWEBVIEW_NAV_ERR_SECURITY"; break;
    case wxWEBVIEW_NAV_ERR_NOT_FOUND: e = "wxWEBVIEW_NAV_ERR_NOT_FOUND"; break;
    case wxWEBVIEW_NAV_ERR_REQUEST: e = "wxWEBVIEW_NAV_ERR_REQUEST"; break;
    case wxWEBVIEW_NAV_ERR_USER_CANCELLED: e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED"; break;
    case wxWEBVIEW_NAV_ERR_OTHER: e = "wxWEBVIEW_NAV_ERR_OTHER"; break;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") % evt.GetURL() % evt.GetTarget() % e % evt.GetString();

    std::string url     = evt.GetURL().ToStdString();
    std::string target  = evt.GetTarget().ToStdString();
    std::string error   = e;
    std::string message = evt.GetString().ToStdString();

    if (message == "COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED") {
        return;
    }
    auto code = evt.GetInt();
    if (code == wxWEBVIEW_NAV_ERR_CONNECTION || code == wxWEBVIEW_NAV_ERR_NOT_FOUND || code == wxWEBVIEW_NAV_ERR_REQUEST) {
        loadFailedPage();
    }
}

void PrinterWebView::OnLoaded(const wxWebViewEvent& evt)
{
    if (m_loadState == PWLoadState::CONNECTING_LOADING) {
        m_loadState = PWLoadState::CONNECTING_LOADED;
        loadInputUrl();
    } else if (m_loadState == PWLoadState::URL_LOADING) {
        m_loadState = PWLoadState::URL_LOADED;
        if (evt.GetURL().IsEmpty())
            return;
        SendAPIKey();
    } else if (m_loadState == PWLoadState::FAILED_LOADING) {
        m_loadState = PWLoadState::FAILED_LOADED;
    }
}
void PrinterWebView::OnScriptMessage(const wxWebViewEvent& event)
{
    // #if defined(__APPLE__) || defined(__MACH__)
    wxString message = event.GetString();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": received message: %s") % into_u8(message);
    // #endif
}

void PrinterWebView::loadConnectingPage()
{
    m_loadState = PWLoadState::CONNECTING_LOADING;
    loadUrl(m_connectiongUrl);
}
void PrinterWebView::loadFailedPage()
{
    if (m_loadState == PWLoadState::URL_LOADED || m_loadState == PWLoadState::URL_LOADING) {
        m_loadState = PWLoadState::FAILED_LOADING;
        // if (!m_isRetry) {
        loadUrl(m_failedUrl + "?url=" + m_url);
        //} else {
        //    m_browser->RunScript(R"(
        //                            function cancelLoading() {
        //                                console.log('cancelLoading');
        //                                window.is_loading_printer = false;
        //                                try{
        //                                    document.getElementById('reconnectButton').disabled = false;
        //                                    document.getElementById('reconnectButton').classList.remove('is-loading');
        //                                }catch(e){
        //                                }
        //                            }
        //                            cancelLoading();
        //                            )");
        //}
    }
}
void PrinterWebView::loadInputUrl()
{
    if (m_loadState == PWLoadState::CONNECTING_LOADED) {
        m_loadState = PWLoadState::URL_LOADING;
        if (!m_url.StartsWith("http://") && !m_url.StartsWith("https://") && !m_url.StartsWith("file://")) {
            loadFailedPage();
            return;
        }
        loadUrl(m_url);
        return;
    }
}
void PrinterWebView::loadUrl(const wxString& url) { m_browser->LoadURL(url); }

void PrinterWebView::runScript(const wxString& javascript)
{
    if (!m_browser)
        return;
    WebView::RunScript(m_browser, javascript);
}

void PrinterWebView::setupIPCHandlers()
{
    if (!mIpc)
        return;

    // handle open url request
    mIpc->onRequest("open", [this](const webviewIpc::IPCRequest& request) {
        auto        params       = request.params;
        std::string url          = params.value("url", "");
        bool        needDownload = params.value("needDownload", false);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": open URL: %s") % url;
        if (needDownload) {
            CallAfter([this, url]() {
                GUI::wxGetApp().download(url);
            });
        } else {
            CallAfter([this, url]() {
                wxLaunchDefaultBrowser(url);
            });
        }
        return webviewIpc::IPCResult::success();
    });

    // handle reload request
    mIpc->onRequest("reload", [this](const webviewIpc::IPCRequest& request) {
        auto params = request.params;
        if (!m_url.IsEmpty()) {
            m_loadState = PWLoadState::URL_LOADING;
            CallAfter([this, url = m_url]() {
                loadUrl(url);
            });
        }
        return webviewIpc::IPCResult::success();
    });

    mIpc->onRequestAsync("open_file_dialog",
                         [this](const webviewIpc::IPCRequest& request, std::function<void(const webviewIpc::IPCResult&)> sendResponse) {
                             auto params = request.params;
                             auto filter = params.value("filter", "All files (*.*)|*.*");

                             wxTheApp->CallAfter([this, filter, sendResponse]() {
                                 try {
                                     wxWindow* parent = this->GetParent();
                                     if (!parent) {
                                         parent = wxGetApp().GetTopWindow();
                                     }

                                     wxFileDialog openFileDialog(parent, _L("Open File"), "", "", filter, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

                                     nlohmann::json data;
                                     if (openFileDialog.ShowModal() != wxID_CANCEL) {
                                         data["files"] = {openFileDialog.GetPath().ToUTF8().data()};
                                     } else {
                                         data["files"] = nlohmann::json::array();
                                     }
                                     sendResponse(webviewIpc::IPCResult::success(data));

                                 } catch (const std::exception& e) {
                                     sendResponse(webviewIpc::IPCResult::error("Failed to open file dialog"));
                                 }
                             });
                         });

    // handle file upload request (using asynchronous event handler)
    mIpc->onRequestAsyncWithEvents("upload_file", [this](const webviewIpc::IPCRequest&                                  request,
                                                         std::function<void(const webviewIpc::IPCResult&)>              sendResponse,
                                                         std::function<void(const std::string&, const nlohmann::json&)> sendEvent) mutable {
        auto params = request.params;

        // check if there is an upload in progress
        if (m_uploadInProgress) {
            sendResponse(webviewIpc::IPCResult::error("Upload already in progress"));
            return;
        }

        try {
            PrinterNetworkParams networkParams;
            networkParams.fileName  = params.value("fileName", "");
            networkParams.filePath  = params.value("filePath", "");
            networkParams.printerId = params.value("printerId", "");

            std::string requestId          = request.id;
            networkParams.uploadProgressFn = [this, requestId, sendEvent](const uint64_t uploadedBytes, const uint64_t totalBytes,
                                                                          bool& cancel) {
                // check if stop is needed
                if (m_shouldStop) {
                    cancel = true;
                    return;
                }

                nlohmann::json data;
                data["uploadedBytes"] = uploadedBytes;
                data["totalBytes"]    = totalBytes;

                // send progress event
                sendEvent("upload_progress", data);
            };

            // set upload status
            m_uploadInProgress = true;
            PrinterNetworkResult<bool> networkResult;
            try {
                networkResult = PrinterManager::getInstance()->upload(networkParams);
            } catch (...) {
                networkResult = PrinterNetworkResult<bool>(PrinterNetworkErrorCode::UNKNOWN_ERROR, false);
            }

            // reset upload status
            m_uploadInProgress = false;
            // send response in main thread
            webviewIpc::IPCResult response;
            response.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
            response.message = networkResult.message;
            if (networkResult.data.has_value()) {
                response.data = networkResult.data.value();
            } else {
                response.data = nlohmann::json::object();
            }
            sendResponse(response);
        } catch (...) {
            m_uploadInProgress = false;
            sendResponse(webviewIpc::IPCResult::error("Upload initialization failed"));
        }
    });

     mIpc->onRequest("getConnectStatus", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        auto printerNetworkInfo = PrinterManager::getInstance()->getPrinterNetworkInfo(printerId);
        nlohmann::json data;
        data["printerId"] = printerId;
        if(printerNetworkInfo.printerId.empty()) {    
            data["connectStatus"] = PRINTER_CONNECT_STATUS_DISCONNECTED;
        } else {
            data["connectStatus"] = printerNetworkInfo.connectStatus;
        }
        webviewIpc::IPCResult result;
        result.data = data;
        result.message = "success";
        result.code = 0;
        return result;
    });
    mIpc->onRequest("getRtcToken", [this](const webviewIpc::IPCRequest& request){
        auto rtcToken = UserNetworkManager::getInstance()->getRtcToken();
        webviewIpc::IPCResult result;
        result.message = rtcToken.message;
        result.code = rtcToken.isSuccess() ? 0 : static_cast<int>(rtcToken.code);
        // Only populate data if the request was successful
        if (rtcToken.isSuccess() && rtcToken.data.has_value()) {
            nlohmann::json data;
            data["rtcToken"]           = rtcToken.data.value().rtcToken;
            data["userId"]             = rtcToken.data.value().userId;
            data["rtcTokenExpireTime"] = rtcToken.data.value().rtcTokenExpireTime;
            result.data                = data;
        } else {
            result.data = nlohmann::json::object();
        }
        return result;
    });
    mIpc->onRequest("sendRtmMessage", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        std::string p = params.dump();
        std::string message = params.value("message", "");
        auto sendRtmMessage = PrinterManager::getInstance()->sendRtmMessage(printerId, message);
        webviewIpc::IPCResult result;
        result.message = sendRtmMessage.message;
        result.code = sendRtmMessage.isSuccess() ? 0 : static_cast<int>(sendRtmMessage.code);
        return result;
    });
    mIpc->onRequest("getFileList", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        int pageNumber = params.value("pageNumber", 1);
        int pageSize = params.value("pageSize", 10);
        auto fileResponse = PrinterManager::getInstance()->getFileList(printerId, pageNumber, pageSize);
        webviewIpc::IPCResult result;
        nlohmann::json fileListJson;
        fileListJson["totalFiles"] = 0;
        fileListJson["fileList"] = nlohmann::json::array();
        if(fileResponse.hasData()) {
            fileListJson["totalFiles"] = fileResponse.data.value().totalFiles;
            for(auto& file : fileResponse.data.value().fileList) {
                nlohmann::json fileJson;
                fileJson["fileId"] = file.fileName;
                fileJson["fileName"] = file.fileName;
                fileJson["printTime"] = file.printTime;
                fileJson["layer"] = file.layer;
                fileJson["layerHeight"] = file.layerHeight;
                fileJson["thumbnail"] = file.thumbnail;
                fileJson["size"] = file.size;
                fileJson["createTime"] = file.createTime;
                fileJson["totalFilamentUsed"] = file.totalFilamentUsed;
                fileJson["totalFilamentUsedLength"] = file.totalFilamentUsedLength;
                fileJson["totalPrintTimes"] = file.totalPrintTimes;
                fileJson["lastPrintTime"] = file.lastPrintTime;           
                fileListJson["fileList"].push_back(fileJson);
            }
        }
        result.data = fileListJson;
        result.message = fileResponse.message;
        result.code = fileResponse.isSuccess() ? 0 : static_cast<int>(fileResponse.code);
        return result;
    });
    mIpc->onRequest("getPrintTaskList", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        int pageNumber = params.value("pageNumber", 1);
        int pageSize = params.value("pageSize", 10);
        auto printTaskResponse = PrinterManager::getInstance()->getPrintTaskList(printerId, pageNumber, pageSize);
        webviewIpc::IPCResult result;
        nlohmann::json printTaskListJson;
        printTaskListJson["totalTasks"] = 0;
        printTaskListJson["taskList"] = nlohmann::json::array();
        if(printTaskResponse.hasData()) {
            printTaskListJson["totalTasks"] = printTaskResponse.data.value().totalTasks;
            for(auto& printTask : printTaskResponse.data.value().taskList) {
                nlohmann::json printTaskJson;
                printTaskJson["taskId"] = printTask.taskId;
                printTaskJson["fileName"] = printTask.fileName;
                printTaskJson["thumbnail"] = printTask.thumbnail;
                printTaskJson["taskName"] = printTask.taskName;
                printTaskJson["totalTime"] = printTask.totalTime;
                printTaskJson["currentTime"] = printTask.currentTime;
                printTaskJson["estimatedTime"] = printTask.estimatedTime;
                printTaskJson["beginTime"] = printTask.beginTime;
                printTaskJson["endTime"] = printTask.endTime;
                printTaskJson["progress"] = printTask.progress;
                printTaskJson["taskStatus"] = printTask.taskStatus;
                printTaskListJson["taskList"].push_back(printTaskJson);
            }
        }
        result.data = printTaskListJson;
        result.message = printTaskResponse.message;
        result.code = printTaskResponse.isSuccess() ? 0 : static_cast<int>(printTaskResponse.code);
        return result;
    });
    mIpc->onRequest("deletePrintTasks", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        std::vector<std::string> taskIds = params.value("taskIds", std::vector<std::string>());
        auto deletePrintTasks = PrinterManager::getInstance()->deletePrintTasks(printerId, taskIds);
        webviewIpc::IPCResult result;
        result.message = deletePrintTasks.message;
        result.code = deletePrintTasks.isSuccess() ? 0 : static_cast<int>(deletePrintTasks.code);
        //package the result data
        nlohmann::json data;
        data["error_code"] =  result.code;
        data["error_message"] = result.message;
        result.data = data;
        return result;
    });

    mIpc->onRequest("getFileDetail", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        std::string fileName = params.value("fileName", "");
        auto fileDetail = PrinterManager::getInstance()->getFileDetail(printerId, fileName);
        webviewIpc::IPCResult result;
        nlohmann::json fileDetailJson;
        if(fileDetail.isSuccess() && fileDetail.hasData()) {
            for(auto& file : fileDetail.data.value().fileList) {
                fileDetailJson["fileName"] = file.fileName;
                fileDetailJson["printTime"] = file.printTime;
                fileDetailJson["layer"] = file.layer;
                fileDetailJson["layerHeight"] = file.layerHeight;
                fileDetailJson["thumbnail"] = file.thumbnail;
                fileDetailJson["size"] = file.size;
                fileDetailJson["createTime"] = file.createTime;
                fileDetailJson["totalFilamentUsed"] = file.totalFilamentUsed;
                fileDetailJson["totalFilamentUsedLength"] = file.totalFilamentUsedLength;
                fileDetailJson["totalPrintTimes"] = file.totalPrintTimes;
                fileDetailJson["lastPrintTime"] = file.lastPrintTime;      
                for(auto& filamentMmsMapping : file.filamentMmsMappingList) {
                    nlohmann::json filamentMmsMappingJson;
                    filamentMmsMappingJson["color"] = filamentMmsMapping.filamentColor;
                    filamentMmsMappingJson["type"] = filamentMmsMapping.filamentType;
                    filamentMmsMappingJson["t"] = filamentMmsMapping.index;
                    fileDetailJson["colorMapping"].push_back(filamentMmsMappingJson);
                }
                break;
            }
        }
        result.data = fileDetailJson;
        result.message = fileDetail.message;
        result.code = fileDetail.isSuccess() ? 0 : static_cast<int>(fileDetail.code);
        return result;
    });
    
    mIpc->onRequest("getPrinterStatusRaw", [this](const webviewIpc::IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        auto statusRaw = PrinterManager::getInstance()->getPrinterStatusRaw(printerId);
        webviewIpc::IPCResult result;
        result.message = statusRaw.message;
        result.code = statusRaw.isSuccess() ? 0 : static_cast<int>(statusRaw.code);
        if (statusRaw.isSuccess() && statusRaw.data.has_value()) {
            // Parse the JSON string and set as data
            try {
                result.data = nlohmann::json::parse(statusRaw.data.value());
            } catch (const std::exception& e) {
                result.code = static_cast<int>(PrinterNetworkErrorCode::PRINTER_NETWORK_INVALID_DATA);
                result.message = "Failed to parse status data, invalid JSON format";
            }
        } else {
            result.data = nlohmann::json::object();
        }
        return result;
    });
}

void PrinterWebView::onRtcTokenChanged(const nlohmann::json& data){
    mIpc->sendEvent("onRtcTokenChanged", data, mIpc->generateRequestId());
}
void PrinterWebView::onRtmMessage(const nlohmann::json& data){
    mIpc->sendEvent("onRtmMessage", data, mIpc->generateRequestId());
}
void PrinterWebView::onConnectionStatus(const nlohmann::json& data){
    mIpc->sendEvent("onConnectionStatus", data, mIpc->generateRequestId());
}
void PrinterWebView::onPrinterEventRaw(const nlohmann::json& data){
    mIpc->sendEvent("onPrinterEventRaw", data, mIpc->generateRequestId());
}
}} // namespace Slic3r::GUI
