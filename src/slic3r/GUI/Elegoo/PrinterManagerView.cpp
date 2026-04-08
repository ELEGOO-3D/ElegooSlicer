#include "PrinterManagerView.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/dc.h>
#include <wx/bitmap.h>
#include <wx/aui/auibook.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibar.h>
#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/webview.h>
#include "slic3r/Utils/PrintHost.hpp"
#include "slic3r/Utils/Elegoo/IPCServer.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/WebviewIPCManager.h"
#include <boost/nowide/fstream.hpp>
#include <mutex>
#include "slic3r/Utils/Elegoo/PrinterNetworkEvent.hpp"
#include "slic3r/Utils/Elegoo/UserNetworkManager.hpp"
#include "slic3r/Utils/Elegoo/PrinterManager.hpp"
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include "slic3r/Utils/Elegoo/MultiInstanceCoordinator.hpp"

#define FIRST_TAB_NAME _L("Connected Printer")
#define TAB_MAX_WIDTH 200
#define TAB_MIN_WIDTH 20
#define TAB_PADDING 8
#define TAB_ICON_TEXT_SPACING 8
#define TAB_CLOSE_BUTTON_SIZE 20
#define TAB_CLOSE_BUTTON_MARGIN 6
#define TAB_SEPARATOR_WIDTH 1
#define TAB_HEIGHT 28
#define TAB_BORDER_WIDTH 1

namespace Slic3r {
namespace GUI {

// Static mutex for tab state file operations
static std::mutex s_tabStateMutex;

class TabArt : public wxAuiSimpleTabArt
{
private:
    bool isDarkMode() const {
        return GUI_App::dark_mode();
    }

    wxAuiNotebook* getNotebookFrom(wxWindow* wnd) const {
        wxWindow* current = wnd;
        while (current) {
            if (auto nb = dynamic_cast<wxAuiNotebook*>(current)) {
                return nb;
            }
            current = current->GetParent();
        }
        return nullptr;
    }

    int calculateTabWidth(wxWindow* wnd, bool isFirstTab) const {
        if (isFirstTab) {
            return TAB_MAX_WIDTH;
        }
        
        wxAuiNotebook* notebook = getNotebookFrom(wnd);
        if (!notebook) {
            return TAB_MAX_WIDTH;
        }
        
        const int totalPages = notebook->GetPageCount();
        const int otherPages = totalPages - 1; // Exclude first tab
        
        if (otherPages <= 0) {
            return TAB_MAX_WIDTH;
        }
        
        const int totalWidth = notebook->GetClientSize().x;
        const int availableWidth = totalWidth - TAB_MAX_WIDTH;
        
        if (availableWidth <= 0) {
            return TAB_MIN_WIDTH;
        }
        
        const int avgWidth = availableWidth / otherPages;
        return std::max(TAB_MIN_WIDTH, std::min(TAB_MAX_WIDTH, avgWidth));
    }

    void drawTabContent(wxDC& dc, const wxRect& tab_rect, const wxBitmap& icon, 
                       const wxString& text, const wxColour& text_colour, bool isFirstTab) const {
        dc.SetTextForeground(text_colour);
        wxSize icon_size = icon.IsOk() ? icon.GetSize() : wxSize(0, 0);
        if (isFirstTab) {
            icon_size = wxSize(16, 16);
        } else {
            icon_size = wxSize(22, 12);
        }
       
        const wxSize text_size = dc.GetTextExtent(text);
        
        // Calculate positions
        const int icon_x = tab_rect.x + TAB_PADDING;
        const int text_x = icon_x + icon_size.x + (icon.IsOk() ? TAB_ICON_TEXT_SPACING : 0);
        const int icon_y = tab_rect.y + (tab_rect.height - icon_size.y) / 2;
        const int text_y = tab_rect.y + (tab_rect.height - text_size.y) / 2;
        
        // Draw icon
        if (icon.IsOk()) {
            dc.DrawBitmap(icon, icon_x, icon_y);
        }
        
        // Draw text with clipping (no ellipsis)
        const int close_button_space = isFirstTab ? 0 : (TAB_CLOSE_BUTTON_SIZE + TAB_CLOSE_BUTTON_MARGIN);
        const int text_max_width = tab_rect.x + tab_rect.width - text_x - close_button_space;
        
        if (text_max_width > 0) {
            wxRect text_clip(text_x, tab_rect.y, text_max_width, tab_rect.height);
            dc.SetClippingRegion(text_clip);
            dc.DrawText(text, text_x, text_y);
            dc.DestroyClippingRegion();
        }
    }

    void drawCloseButton(wxDC& dc, const wxRect& tab_rect, int tabWidth, 
                        int close_button_state, wxRect* out_button_rect) const {
        wxRect close_rect(
            tab_rect.x + tabWidth - TAB_CLOSE_BUTTON_SIZE - TAB_CLOSE_BUTTON_MARGIN,
            tab_rect.y + (tab_rect.height - TAB_CLOSE_BUTTON_SIZE) / 2,
            TAB_CLOSE_BUTTON_SIZE,
            TAB_CLOSE_BUTTON_SIZE
        );
        
        if (out_button_rect) *out_button_rect = close_rect;
        
        // Simple SVG with color modification
        wxBitmap close_icon = create_scaled_bitmap("topbar_close", nullptr, TAB_CLOSE_BUTTON_SIZE);
        if (close_icon.IsOk()) {
            auto close_icon_ = wxBitmap(close_icon.ConvertToImage().Rescale(TAB_CLOSE_BUTTON_SIZE, TAB_CLOSE_BUTTON_SIZE));
            // Change color based on state
            if (close_button_state == wxAUI_BUTTON_STATE_HOVER || close_button_state == wxAUI_BUTTON_STATE_PRESSED) {
                wxImage img = close_icon_.ConvertToImage();
                wxColour new_color = isDarkMode() ? wxColour(255, 120, 120) : wxColour(200, 0, 0);
                
                // Simple color replacement: replace dark pixels with new color
                for (int y = 0; y < img.GetHeight(); y++) {
                    for (int x = 0; x < img.GetWidth(); x++) {
                        if (img.GetAlpha(x, y) > 0) { // Only process non-transparent pixels
                            img.SetRGB(x, y, new_color.Red(), new_color.Green(), new_color.Blue());
                        }
                    }
                }
                close_icon_ = wxBitmap(img);
            }
            
            const int icon_x = close_rect.x + (close_rect.width - close_icon_.GetWidth()) / 2;
            const int icon_y = close_rect.y + (close_rect.height - close_icon_.GetHeight()) / 2;
            dc.DrawBitmap(close_icon_, icon_x, icon_y);
        }
    }

    void getColorScheme(wxColour& activeTab, wxColour& inactiveTab, wxColour& hoverTab, wxColour& activeText, 
                       wxColour& inactiveText, wxColour& background, wxColour& border, wxColour &tabHeaderBackground,
                       wxColour& separator) const {
        if (isDarkMode()) {
            // dark mode color scheme
            activeTab = wxColour(107, 107, 107);     
            inactiveTab = wxColour(45, 45, 48);   // inactive tab: light gray
            hoverTab = wxColour(70, 70, 70);      // hover tab: medium gray
            activeText = wxColour(255, 255, 255);    // active tab text: white
            inactiveText = wxColour(200, 200, 200);  // inactive tab text: light gray
            background = wxColour(45, 45, 45);       // tab bar background: dark gray
            border =  wxColour(0, 0, 0);           // border: black
            tabHeaderBackground = wxColour(45, 45, 45);   // tab header background: dark gray
            separator = wxColour(80, 80, 80);        
        } else {
            // light mode color scheme
            activeTab = wxColour(107, 107, 107);     
            inactiveTab = wxColour(56, 68, 70);      // inactive tab: dark gray
            hoverTab = wxColour(80, 90, 95);        // hover tab: lighter gray
            activeText = wxColour(255, 255, 255);    // active tab text: white
            inactiveText = wxColour(200, 200, 200);     // inactive tab text: dark gray
            background = wxColour(250, 250, 250);    // tab bar background: almost white
            border = wxColour(0, 0, 0);  // border: black
            tabHeaderBackground = wxColour(56, 68, 70);
            separator = wxColour(120, 120, 120); 
        }
    }

    // Get icon for tab based on caption
    wxBitmap getTabIcon(const wxString& caption) const {
        if (caption == FIRST_TAB_NAME) {
            return create_scaled_bitmap("printer_manager", nullptr, 16);  
        } else {
            return create_scaled_bitmap("elegoo_tab", nullptr, 12);  
        }
        return wxBitmap();
    }

public:
    TabArt() {
        wxColour activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator;
        getColorScheme(activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator);

        SetColour(inactiveTab);        // inactive tab background color
        SetActiveColour(activeTab);    // active tab background color
    }

    wxAuiTabArt* Clone() override { return new TabArt(*this); }
    
    void DrawTab(wxDC& dc,
                 wxWindow* wnd,
                 const wxAuiNotebookPage& page,
                 const wxRect& in_rect,
                 int close_button_state,
                 wxRect* out_tab_rect,
                 wxRect* out_button_rect,
                 int* x_extent) override
    {
        const bool isFirstTab = (page.caption == FIRST_TAB_NAME);
        const bool isActive = page.active;

        // Get color scheme
        wxColour activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator;
        getColorScheme(activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator);
        
        // Calculate tab width
        int tabWidth = calculateTabWidth(wnd, isFirstTab);
  
        wxRect tab_rect = in_rect;
        tab_rect.y = in_rect.y + TAB_BORDER_WIDTH;
        tab_rect.width = tabWidth - TAB_SEPARATOR_WIDTH;   
        tab_rect.height = tab_rect.height - 1 - TAB_BORDER_WIDTH;
        // Get icon and text
        wxBitmap icon = getTabIcon(page.caption);
        wxString text = page.caption;
        wxSize text_size = dc.GetTextExtent(text);
        wxSize icon_size = icon.IsOk() ? icon.GetSize() : wxSize(0, 0);
        

        // Check if mouse is over this tab
        wxPoint mousePos = wnd->ScreenToClient(wxGetMousePosition());
        bool mouseOverTab = tab_rect.Contains(mousePos);
        
        // Select colors based on active state and hover state
        wxColour tab_colour;
        if (isActive) {
            tab_colour = activeTab;
        } else if (mouseOverTab) {
            tab_colour = hoverTab;
        } else {
            tab_colour = inactiveTab;
        }
        
        const wxColour text_colour = isActive ? activeText : inactiveText;
        
        // Draw tab background for active tab or hovered tab except first tab is active
        dc.SetBrush(wxBrush(tab_colour));
        dc.SetPen(wxPen(tab_colour, 0)); 
        if ((!isFirstTab && isActive) || (mouseOverTab && !isActive)) {
            dc.DrawRectangle(tab_rect);
        }
        
        // Draw icon and text
        drawTabContent(dc, tab_rect, icon, text, text_colour, isFirstTab);

        // Draw close button for non-first tabs when mouse is over the tab or tab is active
        if (!isFirstTab && (isActive || mouseOverTab)) {
            drawCloseButton(dc, tab_rect, tabWidth, close_button_state, out_button_rect);
        }
        
        // Draw separator
        DrawTabSeparator(dc, tab_rect.x + tab_rect.width, tab_rect.y, tab_rect.height);
             
        // Set output parameters
        if (out_tab_rect) *out_tab_rect = tab_rect;
        if (x_extent) *x_extent = tabWidth;
    }
    
    void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) override
    {
        wxColour activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator;
        getColorScheme(activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator);

        // Draw header background
        auto headerRect = rect;
        headerRect.y = rect.y + TAB_BORDER_WIDTH;
        dc.SetPen(wxPen(tabHeaderBackground, 0));
        dc.SetBrush(wxBrush(tabHeaderBackground));
        dc.DrawRectangle(headerRect);

        // Draw header bottom border
        dc.SetPen(wxPen(border, TAB_BORDER_WIDTH));
        dc.DrawLine(rect.x, rect.y + rect.height - TAB_BORDER_WIDTH, rect.x + rect.width, rect.y + rect.height - TAB_BORDER_WIDTH);
    }

    int GetBorderWidth(wxWindow* wnd) override {
        return 0; // Reserve space for top and bottom borders
    }

    wxSize GetTabSize(wxDC& dc, wxWindow* wnd, const wxString& caption, const wxBitmap& bitmap, bool active, int close_button_state, int* x_extent) override {
        // Get the default tab size
        wxSize default_size = wxAuiSimpleTabArt::GetTabSize(dc, wnd, caption, bitmap, active, close_button_state, x_extent);  
        // Return custom size with modified height
        return wxSize(default_size.x, TAB_HEIGHT);
    }
    void DrawBorder(wxDC& dc, wxWindow* wnd, const wxRect& rect) override
    {
        wxColour activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator;
        getColorScheme(activeTab, inactiveTab, hoverTab, activeText, inactiveText, background, border, tabHeaderBackground, separator);

        // Draw the main background
        dc.SetBrush(wxBrush(background));
        dc.SetPen(wxPen(background, 0));
        dc.DrawRectangle(rect);

        auto headerRect = rect;
        headerRect.height = TAB_HEIGHT + TAB_BORDER_WIDTH;
        if(headerRect.height <= rect.height) {
            dc.SetPen(wxPen(tabHeaderBackground, 0));
            dc.SetBrush(wxBrush(tabHeaderBackground));
            dc.DrawRectangle(headerRect);
        }
        // Draw header top border
        dc.SetPen(wxPen(border, TAB_BORDER_WIDTH));
        dc.DrawLine(headerRect.x, headerRect.y, headerRect.x + headerRect.width, headerRect.y);
    }

    // Draw separator between tabs
    void DrawTabSeparator(wxDC& dc, int x, int y, int height) const
    {
        wxColour separator_color = isDarkMode() ? wxColour(80, 80, 80) : wxColour(120, 120, 120);
        dc.SetPen(wxPen(separator_color, TAB_SEPARATOR_WIDTH));
        dc.DrawLine(x, y, x, y + height);
    }
};


PrinterManagerView::PrinterManagerView(wxWindow *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    mTabBar = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS | wxBORDER_NONE);
    mTabBar->SetArtProvider(new TabArt());
    mTabBar->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    mainSizer->Add(mTabBar, 1, wxEXPAND);
    SetSizer(mainSizer);
    wxAuiManager *m = (wxAuiManager*)&mTabBar->GetAuiManager();
    m->GetArtProvider()->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 0);

    // Delay creating and loading WebView until window is shown
    // This fixes macOS multi-display rendering issue where WKWebView fails to render
    // on extended displays if loaded before the window is fully displayed
    // The WebView will be created and loaded by calling initializeWebView() after window is shown
    mBrowser = nullptr;
    // Note: loadTabState() will be called in initializeWebView() after WebView is created

    Bind(wxEVT_CLOSE_WINDOW, &PrinterManagerView::onClose, this);
    mTabBar->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &PrinterManagerView::onClosePrinterTab, this);
    mTabBar->Bind(wxEVT_AUINOTEBOOK_BEGIN_DRAG, &PrinterManagerView::onTabBeginDrag, this);
    mTabBar->Bind(wxEVT_AUINOTEBOOK_DRAG_MOTION, &PrinterManagerView::onTabDragMotion, this);
    mTabBar->Bind(wxEVT_AUINOTEBOOK_END_DRAG, &PrinterManagerView::onTabEndDrag, this);
    mTabBar->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &PrinterManagerView::onTabChanged, this);
    BOOST_LOG_TRIVIAL(info) << "PrinterManagerView: constructed (WebView deferred)";
}

void PrinterManagerView::initializeWebView()
{
    // Only initialize once
    bool expected = false;
    if (!mWebViewInitialized.compare_exchange_strong(expected, true)) {
        return;
    }
    
    // Create WebView if not already created
    if (!mBrowser) {
        mBrowser = WebView::CreateWebView(mTabBar, "");
        if (mBrowser == nullptr) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": could not init mBrowser";
            // Reset flag on failure to allow retry
            mWebViewInitialized = false;
            return;
        }
        
        mIpc = std::make_unique<webviewIpc::WebviewIPCManager>(mBrowser);
        setupIPCHandlers();
        
        // Check if page already exists in tab bar to avoid duplicate addition
        int pageIndex = mTabBar->GetPageIndex(mBrowser);
        if (pageIndex == wxNOT_FOUND) {
            mTabBar->AddPage(mBrowser, FIRST_TAB_NAME);
        }
    }
    
    // Configure and load URL
    mBrowser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode"));
    
    // Load URL
    auto _dir = resources_dir();
    std::replace(_dir.begin(), _dir.end(), '\\', '/');
    wxString TargetUrl = from_u8((boost::filesystem::path(resources_dir()) / "web/printer/printer_manager/printer.html").make_preferred().string());
    TargetUrl = "file://" + TargetUrl;
    wxString strlang = wxGetApp().current_language_code_safe();
    if (strlang != "")
        TargetUrl = wxString::Format("%s?lang=%s", TargetUrl, strlang);
    if(wxGetApp().app_config->get_bool("developer_mode")){
        TargetUrl = TargetUrl + "&dev=true";
    }  
    mBrowser->LoadURL(TargetUrl);
    
    // Set ElegooSlicer UserAgent
    wxString theme = wxGetApp().dark_mode() ? "dark" : "light";
#ifdef __WIN32__
    mBrowser->SetUserAgent(wxString::Format("ElegooSlicer/%s (%s) Mozilla/5.0 (Windows NT 10.0; Win64; x64)", 
        ELEGOOSLICER_VERSION, theme));
#elif defined(__WXMAC__)
    mBrowser->SetUserAgent(wxString::Format("ElegooSlicer/%s (%s) Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)", 
        ELEGOOSLICER_VERSION, theme));
#elif defined(__linux__)
    mBrowser->SetUserAgent(wxString::Format("ElegooSlicer/%s (%s) Mozilla/5.0 (X11; Linux x86_64)", 
        ELEGOOSLICER_VERSION, theme));
#else
    mBrowser->SetUserAgent(wxString::Format("ElegooSlicer/%s (%s) Mozilla/5.0 (compatible; ElegooSlicer)", 
        ELEGOOSLICER_VERSION, theme));
#endif

    // Load saved tab state
    loadTabState();
    
    Layout();
    BOOST_LOG_TRIVIAL(info) << "PrinterManagerView::initializeWebView: complete";
}

PrinterManagerView::~PrinterManagerView() {
    BOOST_LOG_TRIVIAL(info) << "PrinterManagerView: destructor";
    // Save tab state before destruction
    saveTabState();

    if (mConnectStatusChangedHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->connectStatusChanged.disconnect(mConnectStatusChangedHandlerId);
        mConnectStatusChangedHandlerId = 0;
    }
    if (mEventRawChangedHandlerId != 0) {
        PrinterNetworkEvent::getInstance()->eventRawChanged.disconnect(mEventRawChangedHandlerId);
        mEventRawChangedHandlerId = 0;
    }
    if (mRtcTokenChangedHandlerId != 0) {
        UserNetworkEvent::getInstance()->rtcTokenChanged.disconnect(mRtcTokenChangedHandlerId);
        mRtcTokenChangedHandlerId = 0;
    }
    if (mRtmMessageChangedHandlerId != 0) {
        UserNetworkEvent::getInstance()->rtmMessageChanged.disconnect(mRtmMessageChangedHandlerId);
        mRtmMessageChangedHandlerId = 0;
    }

    std::lock_guard<std::mutex> lock(mPrinterViewsMutex);
    mPrinterViews.clear();
}

void PrinterManagerView::openPrinterTab(const std::string& printerId, bool saveState)
{
    PrinterWebView* existingView = findPrinterView(printerId);
    if (existingView) {
        int idx = mTabBar->GetPageIndex(existingView);
        if (idx != wxNOT_FOUND) {
            mTabBar->SetSelection(idx);
            Layout();
            return;
        }
    }
    std::vector<PrinterNetworkInfo> printerList = PrinterManager::getInstance()->getPrinterList();
    PrinterNetworkInfo printerInfo;
    for (auto& printer : printerList) {
        if (printer.printerId == printerId) {
            printerInfo = printer;
            break;  
        }
    }
    if (printerInfo.printerId.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": printer %s not found") % printerId;
        return;
    }

    wxString url = from_u8(printerInfo.webUrl);
    if(url.IsEmpty()) {
        return;
    }
    PrinterWebView* view = new PrinterWebView(mTabBar);

    if(PrintHost::get_print_host_type(printerInfo.hostType) == htElegooLink && (printerInfo.printerModel == "Elegoo Centauri Carbon 2")) 
    {
        std::string accessCode = printerInfo.accessCode;
        url = url + wxString("?id=") + from_u8(printerInfo.printerId) + "&ip=" + printerInfo.host +"&sn=" + from_u8(printerInfo.serialNumber) + "&access_code=" + accessCode;
    }

    // Get the login region option and add it to the URL parameters
    std::string region = wxGetApp().app_config->get("region");
    if (!region.empty()) {
        // Convert region value to URL encoded format
        std::string region_encoded = wxGetApp().url_encode(region);
        
        // Check if the URL already contains parameters
        if (url.Contains("?")) {
            url += "&region=" + from_u8(region_encoded);
        } else {
            url += "?region=" + from_u8(region_encoded);
        }
    }

    // Add current language parameter
    wxString lang = wxGetApp().current_language_code_safe();
    if (!lang.empty()) {
        if (url.Contains("?")) {
            url += "&lang=" + lang;
        } else {
            url += "?lang=" + lang;
        }
    }

    if(wxGetApp().app_config->get_bool("developer_mode")){
        if(!url.Contains("?"))
        {
            url = url + "?dev=true";
        }
        else
        {
            url = url + "&dev=true";
        }
    }  

    view->load_url(url);
    // Local network shows IP address, cloud printing shows printer name
    if(printerInfo.networkType==0)
    {
        mTabBar->AddPage(view, from_u8(printerInfo.host));
    }else {
        mTabBar->AddPage(view, from_u8(printerInfo.printerName));
    }
    mTabBar->SetSelection(mTabBar->GetPageCount() - 1);
    insertPrinterView(printerId, view);
    Layout();
    
    // Update tab state after adding new tab
    if(saveState)
        saveTabState();
}

void PrinterManagerView::refreshUserInfo()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();
    if (mIpc && mIsReady) {
    // Send refresh signal to navigation webview via IPC
        nlohmann::json data = convertUserNetworkInfoToJson(userNetworkInfo);
        mRefreshUserInfo = UserNetworkInfo();
        mIpc->sendEvent("onUserInfoUpdated", data, mIpc->generateRequestId());
    } else {
        mRefreshUserInfo = userNetworkInfo;
    }
}

void PrinterManagerView::onClose(wxCloseEvent& evt)
{
    // this->Hide();
}

void PrinterManagerView::onClosePrinterTab(wxAuiNotebookEvent& event)
{
    int page = event.GetSelection();
    if (page == wxNOT_FOUND) return;

    wxWindow* win = mTabBar->GetPage(page);
    PrinterWebView* viewToClose = removePrinterViewByWindow(win);
    if (viewToClose) {
        viewToClose->OnClose(wxCloseEvent());
        mTabBar->SetSelection(0);
    }
    
    // Update tab state after closing tab
    saveTabState();
    
    event.Skip();
}

void PrinterManagerView::onTabBeginDrag(wxAuiNotebookEvent& event)
{
    // Prevent dragging the first tab (index 0)
    if ( event.GetSelection() == 0  ) {
        event.Veto();
        mFirstTabClicked = true;
        return;
    }
    mFirstTabClicked = false;
    event.Skip();
}

void PrinterManagerView::onTabDragMotion(wxAuiNotebookEvent& event)
{
    if (mFirstTabClicked) {
        event.Veto();
        return;
    }
    // Prevent dropping at position 0
    wxPoint screenPt = wxGetMousePosition();
    wxPoint nbPt = mTabBar->ScreenToClient(screenPt);
    long flags = 0;
    int targetIndex = mTabBar->HitTest(nbPt, &flags);   
    if (targetIndex == 0) {
        event.Veto();
        return;
    }
    event.Skip();
}

void PrinterManagerView::onTabEndDrag(wxAuiNotebookEvent& event)
{
    if (mFirstTabClicked) {
        event.Veto();
        return;
    }   
    saveTabState();
    event.Skip();
}

void PrinterManagerView::onTabChanged(wxAuiNotebookEvent& event)
{
    // Save tab state when active tab changes
    saveTabState();
    event.Skip();
}



void PrinterManagerView::setupIPCHandlers()
{
    if (!mIpc) return;

    // Handle request_printer_list
    mIpc->onRequest("request_printer_list", [this](const IPCRequest& request){
        return getPrinterList();
    });

    // Handle request_printer_model_list
    mIpc->onRequest("request_printer_model_list", [this](const IPCRequest& request){
        return getPrinterModelList();
    });


    // Handle request_printer_detail
    mIpc->onRequest("request_printer_detail", [this](const IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        if (!printerId.empty()) {
            wxGetApp().CallAfter([this, printerId]() {
                openPrinterTab(printerId);
            });
        }
        return IPCResult::success();
    });

    // Handle request_discover_printers (async due to time-consuming operation)
    mIpc->onRequestAsync("request_discover_printers", [this](const IPCRequest& request, 
                                                           std::function<void(const IPCResult&)> sendResponse) {  
        try {
            // Call the static method to avoid accessing this pointer
            auto result = this->discoverPrinter();
            sendResponse(result);
        } catch (const std::exception& e) {
            sendResponse(IPCResult::error(std::string("Discovery failed: ") + e.what()));
        }
    });

    // Handle request_add_printer (async)
    mIpc->onRequestAsync("request_add_printer", [this](const IPCRequest& request,
                                                        std::function<void(const IPCResult&)> sendResponse) {  
        auto params = request.params;
        if (!params.contains("printer")) {
            sendResponse(IPCResult::error("Missing printer parameter"));
            return;
        }
        nlohmann::json printer = params["printer"];
        
        try {
            auto result = addPrinter(printer);
            sendResponse(result);        
        } catch (const std::exception& e) {
            sendResponse(IPCResult::error(std::string("Add printer failed: ") + e.what()));
        } catch (...) {
            sendResponse(IPCResult::error("Add printer failed: Unknown error"));
        }
    });

    // Handle request_cancel_add_printer
    mIpc->onRequestAsync("request_cancel_add_printer", [this](const IPCRequest& request,
                                                                 std::function<void(const IPCResult&)> sendResponse) {
        auto params = request.params;
        if (!params.contains("printer")) {
            sendResponse(IPCResult::error("Missing printer parameter"));
            return;
        }
        nlohmann::json printer = params["printer"];
        try {
            auto result = cancelBindPrinter(printer);
            sendResponse(result);
        } catch (const std::exception& e) {
            sendResponse(IPCResult::error(std::string("Cancel bind printer failed: ") + e.what()));
        } catch (...) {
            sendResponse(IPCResult::error("Cancel bind printer failed: Unknown error"));
        }
    });
    // Handle request_add_physical_printer (async)
    mIpc->onRequestAsync("request_add_physical_printer", [this](const IPCRequest& request,
                                                                 std::function<void(const IPCResult&)> sendResponse) {
      
        auto params = request.params;
        if (!params.contains("printer")) {
            sendResponse(IPCResult::error("Missing printer parameter"));
            return;
        }
        
        nlohmann::json printer = params["printer"];
            try {
                auto result = addPhysicalPrinter(printer);   
                sendResponse(result);
            } catch (const std::exception& e) {
                sendResponse(IPCResult::error(std::string("Add physical printer failed: ") + e.what()));
            } catch (...) {
                sendResponse(IPCResult::error("Add physical printer failed: Unknown error"));
            }
    });

    // Handle request_update_printer_name
    mIpc->onRequest("request_update_printer_name", [this](const IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        std::string printerName = params.value("printerName", "");
        return updatePrinterName(printerId, printerName);
    });

    // Handle request_update_physical_printer
    mIpc->onRequestAsync("request_update_physical_printer", [this](const IPCRequest& request,
                                                                    std::function<void(const IPCResult&)> sendResponse) {
        auto params = request.params;
        if (!params.contains("printerId") || !params.contains("printer")) {
            sendResponse(IPCResult::error("missing printerId or printer parameter"));
            return;
        }
        
        std::string printerId = params.value("printerId", "");
        nlohmann::json printer = params["printer"];

        try {
            auto result = updatePhysicalPrinter(printerId, printer);
            sendResponse(result);
        } catch (const std::exception& e) {
            sendResponse(IPCResult::error(std::string("update physical printer failed: ") + e.what()));
        } catch (...) {
            sendResponse(IPCResult::error("update physical printer failed: unknown error"));
        }
    });

    // Handle request_update_printer_host
    mIpc->onRequestAsync("request_update_printer_host", [this](const IPCRequest& request,
                                                                std::function<void(const IPCResult&)> sendResponse) {
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        std::string host = params.value("host", "");
 
            try {
                auto result = updatePrinterHost(printerId, host);
                sendResponse(result);
            } catch (const std::exception& e) {
                sendResponse(IPCResult::error(std::string("Update printer host failed: ") + e.what()));
            } catch (...) {
                sendResponse(IPCResult::error("Update printer host failed: Unknown error"));    
            }
    });

    // Handle request_delete_printer
    mIpc->onRequest("request_delete_printer", [this](const IPCRequest& request){
        auto params = request.params;
        std::string printerId = params.value("printerId", "");
        return deletePrinter(printerId);
    });

    // Handle request_browse_ca_file (async because it shows a file dialog)
    mIpc->onRequestAsync("request_browse_ca_file", [this](const IPCRequest& request,
                                                          std::function<void(const IPCResult&)> sendResponse) {
        wxGetApp().CallAfter([this, sendResponse]() {
            try {
                IPCResult result = browseCAFile();
                sendResponse(result);
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": error in browseCAFile: %s") % e.what();
                sendResponse(IPCResult::error(std::string("Failed to browse CA file: ") + e.what()));
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": unknown error in browseCAFile";
                sendResponse(IPCResult::error("Failed to browse CA file: Unknown error"));
            }
        });
    });

    // Handle request_refresh_printers
    mIpc->onRequest("request_refresh_printers", [this](const IPCRequest& request){
        // Implementation for refresh printers
        return IPCResult::success();
    });

    // Handle request_refresh_wan_printers
    mIpc->onRequest("request_refresh_wan_printers", [this](const IPCRequest& request){
        PrinterManager::getInstance()->enqueueWanSyncRequest();
        return IPCResult::success();
    });
    // Handle request_logout_print_host
    mIpc->onRequest("request_logout_print_host", [this](const IPCRequest& request){
        // Implementation for logout print host
        return IPCResult::success();
    });

    // Handle request_connect_physical_printer
    mIpc->onRequest("request_connect_physical_printer", [this](const IPCRequest& request){
        // Implementation for connect physical printer
        return IPCResult::success();
    });

    mIpc->onRequest("checkLoginStatus", [this](const IPCRequest& request){
        return handleCheckLoginStatus();
    });

    mIpc->onRequest("ready", [this](const IPCRequest& request){
        return handleReady();
    });

    mIpc->onRequest("request_user_info", [this](const IPCRequest& request){
        UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();   
        nlohmann::json data = convertUserNetworkInfoToJson(userNetworkInfo);
        return IPCResult::success(data);
    });

    mIpc->onRequest("get_license_expired_devices", [this](const IPCRequest& request){
        auto licenseResult = PrinterManager::getInstance()->getLicenseExpiredDevices();
        IPCResult result;
        nlohmann::json devicesJson;
        devicesJson["devices"] = nlohmann::json::array();
        if (licenseResult.hasData()) {
            for (const auto& device : licenseResult.data.value()) {
                nlohmann::json deviceJson;
                deviceJson["serialNumber"] = device.serialNumber;
                deviceJson["status"] = device.status;
                devicesJson["devices"].push_back(deviceJson);
            }
        }
        result.data = devicesJson;
        result.message = licenseResult.message;
        result.code = licenseResult.isSuccess() ? 0 : static_cast<int>(licenseResult.code);
        return result;
    });
    
    mIpc->onRequest("renew_license", [this](const IPCRequest& request){
        auto params = request.params;
        std::string serialNumber = params.value("serialNumber", "");
        auto renewResult = PrinterManager::getInstance()->renewLicense(serialNumber);
        IPCResult result;
        result.message = renewResult.message;
        result.code = renewResult.isSuccess() ? 0 : static_cast<int>(renewResult.code);
        return result;
    });

    mIpc->onRequest("get_license_expired_devices", [this](const IPCRequest& request){
        auto licenseResult = PrinterManager::getInstance()->getLicenseExpiredDevices();
        IPCResult result;
        nlohmann::json devicesJson;
        devicesJson["devices"] = nlohmann::json::array();
        if (licenseResult.hasData()) {
            for (const auto& device : licenseResult.data.value()) {
                nlohmann::json deviceJson;
                deviceJson["serialNumber"] = device.serialNumber;
                deviceJson["status"] = device.status;
                devicesJson["devices"].push_back(deviceJson);
            }
        }
        result.data = devicesJson;
        result.message = licenseResult.message;
        result.code = licenseResult.isSuccess() ? 0 : static_cast<int>(licenseResult.code);
        return result;
    });
    
    mIpc->onRequest("renew_license", [this](const IPCRequest& request){
        auto params = request.params;
        std::string serialNumber = params.value("serialNumber", "");
        auto renewResult = PrinterManager::getInstance()->renewLicense(serialNumber);
        IPCResult result;
        result.message = renewResult.message;
        result.code = renewResult.isSuccess() ? 0 : static_cast<int>(renewResult.code);
        return result;
    });
    
    mIpc->onRequest("refresh_printer_status", [this](const IPCRequest& request){
        IPCResult result;
        auto printerList = PrinterManager::getInstance()->getPrinterList(); 
        for (const auto& printer : printerList) {
            auto refreshResult = PrinterManager::getInstance()->refreshPrinterStatus(printer.printerId);
        }
        result.code = 0;
        return result;
    });
    
    mConnectStatusChangedHandlerId = PrinterNetworkEvent::getInstance()->connectStatusChanged.connect([this](const PrinterConnectStatusEvent& event) {
        wxGetApp().CallAfter([this, event] {
            PrinterWebView* targetView = findPrinterView(event.printerId);
            nlohmann::json data;
            data["status"] = event.status;
            data["printerId"] = event.printerId;
            if (targetView) {
                targetView->onConnectionStatus(data);
            }
            if (MultiInstanceCoordinator::getInstance()->isMaster()) {
                IPCServer::getInstance()->broadcastEvent(IPCEvent("printer.connectStatusChanged", data, ""));
            }
        });
    });
    mEventRawChangedHandlerId = PrinterNetworkEvent::getInstance()->eventRawChanged.connect([this](const PrinterEventRawEvent& event) {
        wxGetApp().CallAfter([this, event] {
            PrinterWebView* targetView = findPrinterView(event.printerId);
            nlohmann::json data;
            data["event"] = event.event;
            data["printerId"] = event.printerId;
            if (targetView) {
                targetView->onPrinterEventRaw(data);
            }
            if (MultiInstanceCoordinator::getInstance()->isMaster()) {
                IPCServer::getInstance()->broadcastEvent(IPCEvent("printer.eventRawChanged", data, ""));
            }
        });
    });

    mRtcTokenChangedHandlerId = UserNetworkEvent::getInstance()->rtcTokenChanged.connect([this](const UserRtcTokenEvent& event) {
        wxGetApp().CallAfter([this, event] {
            nlohmann::json data;
            data["rtcToken"] = event.userInfo.rtcToken;
            data["userId"] = event.userInfo.userId;
            data["rtcTokenExpireTime"] = event.userInfo.rtcTokenExpireTime;
            forEachPrinterView([&data](const std::string&, PrinterWebView* view) { view->onRtcTokenChanged(data); });
            if (MultiInstanceCoordinator::getInstance()->isMaster()) {
                IPCServer::getInstance()->broadcastEvent(IPCEvent("user.rtcTokenChanged", data, ""));
            }
        });
    });
    mRtmMessageChangedHandlerId = UserNetworkEvent::getInstance()->rtmMessageChanged.connect([this](const UserRtmMessageEvent& event) {
        wxGetApp().CallAfter([this, event] {
            PrinterWebView* targetView = findPrinterView(event.printerId);
            nlohmann::json data;
            data["message"] = event.message;
            data["printerId"] = event.printerId;
            if (targetView) {
                targetView->onRtmMessage(data);
            }
            if (MultiInstanceCoordinator::getInstance()->isMaster()) {
                IPCServer::getInstance()->broadcastEvent(IPCEvent("user.rtmMessageChanged", data, ""));
            }
        });
    });

}

IPCResult PrinterManagerView::deletePrinter(const std::string& printerId)
{ 
    IPCResult result;
    auto networkResult = PrinterManager::getInstance()->deletePrinter(printerId);
    result.message = networkResult.message;
    result.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
    wxGetApp().CallAfter([this, printerId]() {
        PrinterWebView* view = findPrinterView(printerId);
        if (view) {
            int page = mTabBar->GetPageIndex(view);
            if (page != wxNOT_FOUND) {
                mTabBar->DeletePage(page);
            }
            view->OnClose(wxCloseEvent());
            removePrinterView(printerId);
            mTabBar->SetSelection(0);
        }
    });
    return result;
}
void PrinterManagerView::closeInvalidPrinterTab(std::vector<PrinterNetworkInfo>& printerList)
{
    std::vector<std::string> printersToRemove;
    std::vector<PrinterWebView*> viewsToClose;
    
    forEachPrinterView([&printerList, &printersToRemove, &viewsToClose](const std::string& printerId, PrinterWebView* view) {
        auto it = std::find_if(printerList.begin(), printerList.end(), 
                              [&printerId](const PrinterNetworkInfo& p) { return p.printerId == printerId; });
        if (it == printerList.end()) {
            printersToRemove.push_back(printerId);
            viewsToClose.push_back(view);
        }
    });
    
    wxGetApp().CallAfter([this, printersToRemove, viewsToClose]() {
        for (size_t i = 0; i < printersToRemove.size(); ++i) {
            int page = mTabBar->GetPageIndex(viewsToClose[i]);
            if (page != wxNOT_FOUND) {
                mTabBar->DeletePage(page);
            }
            viewsToClose[i]->OnClose(wxCloseEvent());
            removePrinterView(printersToRemove[i]);
        }
    });

}
IPCResult PrinterManagerView::updatePrinterName(const std::string& printerId, const std::string& printerName)
{
    IPCResult result;
    wxGetApp().CallAfter([this, printerId, printerName]() {
        PrinterWebView* view = findPrinterView(printerId);
        if (view) {
            int page = mTabBar->GetPageIndex(view);
            if (page != wxNOT_FOUND) {
                mTabBar->SetPageText(page, from_u8(printerName));
            }
        }
    });
    auto networkResult = PrinterManager::getInstance()->updatePrinterName(printerId, printerName);
    result.message = networkResult.message;
    result.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
    return result;
}
IPCResult PrinterManagerView::updatePrinterHost(const std::string& printerId, const std::string& host)
{
    IPCResult result;
    auto networkResult = PrinterManager::getInstance()->updatePrinterHost(printerId, host);
    result.message = networkResult.message;
    result.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
    PrinterNetworkInfo printerInfo = PrinterManager::getInstance()->getPrinterNetworkInfo(printerId);
    
    if(result.code == 0 && !printerInfo.host.empty() && printerInfo.host != host) {     
        wxString url = printerInfo.webUrl;
        if(PrintHost::get_print_host_type(printerInfo.hostType) == htElegooLink && (printerInfo.printerModel == "Elegoo Centauri Carbon 2")) 
        {
            std::string accessCode = printerInfo.accessCode;
            url = url + wxString("?id=") + from_u8(printerInfo.printerId) + "&ip=" + printerInfo.host +"&sn=" + from_u8(printerInfo.serialNumber) + "&access_code=" + accessCode;
        }
        wxGetApp().CallAfter([this, printerId, url]() {
            PrinterWebView* view = findPrinterView(printerId);
            if (view) {
                int page = mTabBar->GetPageIndex(view);
                if (page != wxNOT_FOUND) {
                    view->load_url(url);
                }
            }
        });
    }
    return result;
}

IPCResult PrinterManagerView::updatePhysicalPrinter(const std::string& printerId, const nlohmann::json& printer)
{
    IPCResult result;
    PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(printer);

    PrinterNetworkInfo oldPrinter =  PrinterManager::getInstance()->getPrinterNetworkInfo(printerId);
    if (oldPrinter.printerId.empty()) {
        result.code = static_cast<int>(PrinterNetworkErrorCode::PRINTER_NOT_FOUND);
        result.message = getErrorMessage(PrinterNetworkErrorCode::PRINTER_NOT_FOUND);
        return result;
    }

    auto networkResult = PrinterManager::getInstance()->updatePhysicalPrinter(printerId, printerInfo);
    result.message = networkResult.message;
    result.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
    
    if (result.code == 0 && (oldPrinter.host != printerInfo.host || oldPrinter.webUrl != printerInfo.webUrl)) {
        PrinterNetworkInfo updatedPrinter = PrinterManager::getInstance()->getPrinterNetworkInfo(printerId);
        wxGetApp().CallAfter([this, printerId, updatedPrinter]() {
            PrinterWebView* view = findPrinterView(printerId);
            if (view) {
                int page = mTabBar->GetPageIndex(view);
                if (page != wxNOT_FOUND) {
                    mTabBar->SetPageText(page, from_u8(updatedPrinter.printerName));
                    wxString url = updatedPrinter.webUrl;
                    view->load_url(url);
                    view->reload();
                }
            }
        });
    }
    return result;
}

IPCResult PrinterManagerView::addPrinter(const nlohmann::json& printer)
{
    IPCResult result;
    PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(printer);
    printerInfo.isPhysicalPrinter = false;
    auto networkResult = PrinterManager::getInstance()->addPrinter(printerInfo);
    result.message = networkResult.message;
    result.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
    return result;
}
IPCResult PrinterManagerView::addPhysicalPrinter(const nlohmann::json& printer)
{
    IPCResult result;
    PrinterNetworkErrorCode errorCode = PrinterNetworkErrorCode::SUCCESS;
    PrinterNetworkInfo printerInfo;
    try {
        printerInfo = convertJsonToPrinterNetworkInfo(printer);
        printerInfo.isPhysicalPrinter = true;
        auto networkResult = PrinterManager::getInstance()->addPrinter(printerInfo);
        result.message = networkResult.message;
        errorCode = networkResult.code;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": add physical printer error: %s") % e.what();
        errorCode = PrinterNetworkErrorCode::INVALID_PARAMETER;
        result.message = getErrorMessage(errorCode);
    }
    result.code = errorCode == PrinterNetworkErrorCode::SUCCESS ? 0 : static_cast<int>(errorCode);
    return result;
}

IPCResult PrinterManagerView::cancelBindPrinter(const nlohmann::json& printer)
{
    IPCResult result;
    PrinterNetworkInfo printerInfo = convertJsonToPrinterNetworkInfo(printer);
    auto networkResult = PrinterManager::getInstance()->cancelBindPrinter(printerInfo);
    result.message = networkResult.message;
    result.code = networkResult.isSuccess() ? 0 : static_cast<int>(networkResult.code);
    return result;
}
IPCResult PrinterManagerView::discoverPrinter()
{
    IPCResult result;
    auto printerListResult = PrinterManager::getInstance()->discoverPrinter();
    std::vector<PrinterNetworkInfo> printerList;
    if(printerListResult.hasData()) {
        printerList = printerListResult.data.value();
    }
    nlohmann::json response = json::array();
    for (auto& printer : printerList) {
        nlohmann::json printer_obj = nlohmann::json::object();
        printer_obj = convertPrinterNetworkInfoToJson(printer);
        printer_obj["isAdded"] = printer.isAdded;
        boost::filesystem::path resources_path(Slic3r::resources_dir());
        std::string img_path = resources_path.string() + "/profiles/" + printer.vendor + "/" + printer.printerModel + "_cover.png";
        printer_obj["printerImg"] = PrinterManager::imageFileToBase64DataURI(img_path);
        response.push_back(printer_obj);
    }
    result.data = response;
    result.code = printerListResult.isSuccess() ? 0 : static_cast<int>(printerListResult.code);
    result.message = printerListResult.message;
    return result;
}
IPCResult PrinterManagerView::getPrinterList()
{  
    IPCResult result;
    // Cache for printer images (printerId -> base64 image data)
    static std::map<std::string, std::string> printerImageCache;
    auto printerList = PrinterManager::getInstance()->getPrinterList();
    
    // Build set of current printer IDs and process printers in one pass
    std::set<std::string> currentPrinterIds;
    nlohmann::json response = json::array();
    boost::filesystem::path resources_path(Slic3r::resources_dir());
    
    for (auto& printer : printerList) {
        currentPrinterIds.insert(printer.printerId);
        
        nlohmann::json printer_obj = convertPrinterNetworkInfoToJson(printer);
        
        // Check if image is already cached
        auto cacheIt = printerImageCache.find(printer.printerId);
        if (cacheIt == printerImageCache.end()) {
            // Load image and cache it
            std::string img_path = resources_path.string() + "/profiles/" + printer.vendor + "/" + printer.printerModel + "_cover.png";
            printerImageCache[printer.printerId] = PrinterManager::imageFileToBase64DataURI(img_path);
            cacheIt = printerImageCache.find(printer.printerId);
        }
        printer_obj["printerImg"] = cacheIt->second;
        response.push_back(printer_obj);
    }
    
    // Remove cached images for printers that no longer exist
    for (auto it = printerImageCache.begin(); it != printerImageCache.end();) {
        if (currentPrinterIds.find(it->first) == currentPrinterIds.end()) {
            it = printerImageCache.erase(it);
        } else {
            ++it;
        }
    }
    
    closeInvalidPrinterTab(printerList);
    // Return object with printer list and main client status
    nlohmann::json resultData;
    resultData["printers"] = response;
    result.data = resultData;
    result.code = 0;
    result.message = getErrorMessage(PrinterNetworkErrorCode::SUCCESS);
    return result;
}
IPCResult PrinterManagerView::browseCAFile()
{
    IPCResult result;
    PrinterNetworkErrorCode errorCode = PrinterNetworkErrorCode::SUCCESS;
    std::string path = "";
    try {
        static const auto filemasks = _L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*");
        wxFileDialog openFileDialog(this, _L("Open CA certificate file"), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (openFileDialog.ShowModal() != wxID_CANCEL) {
            path = openFileDialog.GetPath().ToStdString();
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": browse CA file error: %s") % e.what();
        errorCode = PrinterNetworkErrorCode::PRINTER_UNKNOWN_ERROR;
    }
    result.data = path;
    result.code = errorCode == PrinterNetworkErrorCode::SUCCESS ? 0 : static_cast<int>(errorCode);
    result.message = getErrorMessage(errorCode);
    return result;
}
IPCResult PrinterManagerView::handleReady()
{
    lock_guard<mutex> lock(mUserInfoMutex);
    mIsReady = true;
    // Send any pending user info with delay to ensure frontend is ready
    if(!mRefreshUserInfo.userId.empty() && mIpc) {
        nlohmann::json data = convertUserNetworkInfoToJson(mRefreshUserInfo);
        mIpc->sendEvent("onUserInfoUpdated", data, mIpc->generateRequestId());
        mRefreshUserInfo = UserNetworkInfo();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": sent pending user info to WebView";
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": ready";
    return IPCResult::success();
}
IPCResult PrinterManagerView::handleCheckLoginStatus()
{
    UserNetworkInfo userNetworkInfo = UserNetworkManager::getInstance()->getUserInfo();
    auto            result          = UserNetworkManager::getInstance()->checkUserNeedReLogin();
    if (result.isSuccess()) {
        bool needReLogin = result.data.value();
        if (needReLogin) {
            // need re-login
            auto evt = new wxCommandEvent(EVT_USER_LOGIN);
            wxQueueEvent(wxGetApp().mainframe, evt);
            return IPCResult::success();
        }
    } else {
        return IPCResult::error(result.message);
    }
    // don't need to re-login, return user info
    nlohmann::json data = convertUserNetworkInfoToJson(userNetworkInfo);
    return IPCResult::success(data);
}
IPCResult PrinterManagerView::getPrinterModelList()
{
    IPCResult result;
    auto vendorPrinterModelConfigMap = PrinterManager::getVendorPrinterModelConfig();   
    nlohmann::json response = nlohmann::json::array();
    for (auto& vendor : vendorPrinterModelConfigMap) {
        nlohmann::json vendorObj = nlohmann::json::object();
        vendorObj["vendor"]      = vendor.first;
        vendorObj["models"]      = nlohmann::json::array();
        for (auto& model : vendor.second) {
            nlohmann::json modelObj = nlohmann::json::object();
            modelObj["modelName"]  = model.first;
            auto config = model.second;
            const auto opt = config.option<ConfigOptionEnum<PrintHostType>>("host_type");
            const auto hostType = opt != nullptr ? opt->value : htOctoPrint;
            std::string hostTypeStr = PrintHost::get_print_host_type_str(hostType);
            if (!hostTypeStr.empty()) {
                modelObj["hostType"]   = hostTypeStr;
            }
            bool supportWanNetwork = false;
            
            if (config.has("support_wan_network")) {
                supportWanNetwork = config.opt_bool("support_wan_network");
            }
            modelObj["supportWanNetwork"] = supportWanNetwork;
            vendorObj["models"].push_back(modelObj);
        }
        response.push_back(vendorObj);
    }
    result.data = response;
    result.code = 0;
    result.message = getErrorMessage(PrinterNetworkErrorCode::SUCCESS);
    return result;
}

void PrinterManagerView::saveTabState()
{
    std::lock_guard<std::mutex> lock(s_tabStateMutex);
    try {
        nlohmann::json tabState = nlohmann::json::object();
        nlohmann::json tabs = nlohmann::json::array();
        
        // Save all printer tabs (skip the first tab which is always "Connected Printer")
        for (size_t i = 1; i < mTabBar->GetPageCount(); ++i) {
            wxWindow* page = mTabBar->GetPage(i);
            if (page) {
                // Find the printer ID for this page
                forEachPrinterView([&page, &tabs, this, i](const std::string& printerId, PrinterWebView* view) {
                    if (view == page) {
                        nlohmann::json tabInfo;
                        tabInfo["printerId"] = printerId;
                        tabInfo["tabName"] = mTabBar->GetPageText(i).ToStdString();
                        tabs.push_back(tabInfo);
                    }
                });
            }
        }
        
        // Save current active tab index
        int activeTabIndex = mTabBar->GetSelection();
        tabState["tabs"] = tabs;
        tabState["activeTabIndex"] = activeTabIndex;
        
        // Save to file
        std::string filePath = (boost::filesystem::path(Slic3r::data_dir()) / "user" / "printer_tab_state.json").string();
        
        // Ensure directory exists
        boost::filesystem::path dir = boost::filesystem::path(filePath).parent_path();
        if (!boost::filesystem::exists(dir)) {
            boost::filesystem::create_directories(dir);
        }
        
        boost::nowide::ofstream file(filePath);
        if (file.is_open()) {
            file << tabState.dump(4);
            file.close();
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to save tab state: %s") % e.what();
    }
}

void PrinterManagerView::loadTabState()
{
    std::lock_guard<std::mutex> lock(s_tabStateMutex);
    try {
        std::string filePath = (boost::filesystem::path(Slic3r::data_dir()) / "user" / "printer_tab_state.json").string();
        
        if (!boost::filesystem::exists(filePath)) {
            return; // No saved state
        }
        
        boost::nowide::ifstream file(filePath);
        if (!file.is_open()) {
            return;
        }
        
        nlohmann::json tabState;
        file >> tabState;
        file.close();
        
        if (!tabState.is_object() || !tabState.contains("tabs")) {
            return;
        }
        
        nlohmann::json tabs = tabState["tabs"];
        if (!tabs.is_array()) {
            return;
        }
        
        // Load tabs in order
        for (const auto& tabInfo : tabs) {
            if (tabInfo.contains("printerId")) {
                std::string printerId = tabInfo["printerId"];
                openPrinterTab(printerId, false);
            }
        }
        int activeTabIndex = 0;
        // Restore active tab after all tabs are loaded
        if (tabState.contains("activeTabIndex")) {
            activeTabIndex = tabState["activeTabIndex"];
      
        }
        if (activeTabIndex >= 0 && activeTabIndex < mTabBar->GetPageCount()) {
            mTabBar->SetSelection(activeTabIndex);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": failed to load tab state: %s") % e.what();
    }
}


PrinterWebView* PrinterManagerView::findPrinterView(const std::string& printerId)
{
    std::lock_guard<std::mutex> lock(mPrinterViewsMutex);
    auto it = mPrinterViews.find(printerId);
    return (it != mPrinterViews.end()) ? it->second : nullptr;
}

void PrinterManagerView::insertPrinterView(const std::string& printerId, PrinterWebView* view)
{
    std::lock_guard<std::mutex> lock(mPrinterViewsMutex);
    mPrinterViews[printerId] = view;
}

bool PrinterManagerView::removePrinterView(const std::string& printerId)
{
    std::lock_guard<std::mutex> lock(mPrinterViewsMutex);
    return mPrinterViews.erase(printerId) > 0;
}

PrinterWebView* PrinterManagerView::removePrinterViewByWindow(wxWindow* win)
{
    std::lock_guard<std::mutex> lock(mPrinterViewsMutex);
    for (auto it = mPrinterViews.begin(); it != mPrinterViews.end(); ++it) {
        if (it->second == win) {
            PrinterWebView* view = it->second;
            mPrinterViews.erase(it);
            return view;
        }
    }
    return nullptr;
}

void PrinterManagerView::forEachPrinterView(std::function<void(const std::string&, PrinterWebView*)> callback)
{
    std::lock_guard<std::mutex> lock(mPrinterViewsMutex);
    for (const auto& pair : mPrinterViews) {
        callback(pair.first, pair.second);
    }
}

} // GUI
} // Slic3r


