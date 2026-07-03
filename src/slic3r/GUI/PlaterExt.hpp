#pragma once

#include "libslic3r/Config.hpp"

#include <string>

namespace Slic3r::GUI::PlaterExt {

void sync_mms_filament();
int auto_load_missing_vendor_presets(DynamicPrintConfig& config, const std::string& filename);

} // namespace Slic3r::GUI::PlaterExt
