#pragma once

#include <string>

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI::PlaterExt {

void sync_mms_filament();
int auto_load_missing_vendor_presets(DynamicPrintConfig& config, const std::string& filename);

} // namespace GUI::PlaterExt
} // namespace Slic3r
