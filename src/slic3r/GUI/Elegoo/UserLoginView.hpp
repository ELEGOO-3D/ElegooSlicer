#pragma once

#include "slic3r/GUI/Elegoo/ElegooDialog.hpp"
#include <atomic>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

class UserLoginView : public ElegooDialog
{
public:
    UserLoginView(wxWindow* parent);
    virtual ~UserLoginView();
    
    static void ShowLoginDialog();

protected:
    void setupIPCHandlers() override;
    void onCloseWindow(wxCloseEvent& event) override;

private:
    std::string mLanguage;
    std::string mRegion;
    
    static std::atomic<bool> s_isShown;
    
    DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI
