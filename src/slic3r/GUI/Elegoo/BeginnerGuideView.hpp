#pragma once

#include "slic3r/GUI/Elegoo/ElegooDialog.hpp"

namespace Slic3r { namespace GUI {

class BeginnerGuideView : public ElegooDialog
{
public:
    BeginnerGuideView(wxWindow* parent);
    virtual ~BeginnerGuideView() = default;
    virtual int ShowModal() override;
};

}} // namespace Slic3r::GUI 
