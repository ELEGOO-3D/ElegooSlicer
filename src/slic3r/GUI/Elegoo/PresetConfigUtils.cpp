#include "PresetConfigUtils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"
#include <set>

namespace Slic3r { namespace GUI {

nlohmann::json PresetConfigUtils::PresetsToJson(const std::vector<std::pair<const Preset*, bool>>& presets)
{
    nlohmann::json j_array = nlohmann::json::array();
    for (const auto& [preset, is_selected] : presets) {
        j_array.push_back(PresetToJson(preset, is_selected));
    }
    return j_array;
}

nlohmann::json PresetConfigUtils::PresetToJson(const Preset* preset, bool is_selected)
{
    nlohmann::json j;
    j["name"] = preset->name;
    j["is_default"] = preset->is_default;
    j["is_selected"] = is_selected;
    j["config"] = preset->config.to_json(preset->name, "", preset->version.to_string(), preset->custom_defined);
    return j;
}

nlohmann::json PresetConfigUtils::GetPresetsJson(Preset::Type type) {
    Tab* tab = wxGetApp().get_tab(type);
    if (!tab) {
        return nlohmann::json::array();
    }

    TabPresetComboBox* combo = tab->get_combo_box();
    PresetCollection* preset_collection = tab->m_presets;
    
    // Get the currently selected preset name(s) for comparison
    // For filament type, we need to check against all selected filaments in filament_presets array
    // because filament selection supports multiple extruders
    std::set<std::string> selected_preset_names;
    if (type == Preset::Type::TYPE_FILAMENT) {
        // For filament, check the actual filament_presets array which stores selected filament for each extruder
        const std::vector<std::string>& filament_presets = wxGetApp().preset_bundle->filament_presets;
        for (const auto& name : filament_presets) {
            selected_preset_names.insert(name);
        }
    } else {
        // For other types, use the preset collection's selected preset
        if (preset_collection) {
            selected_preset_names.insert(preset_collection->get_selected_preset_name());
        }
    }
    
    std::vector<std::pair<const Preset*, bool>> presets;

    for (unsigned int i = 0; i < combo->GetCount(); i++) {
        std::string preset_name = combo->GetString(i).ToUTF8().data();

        if (preset_name.substr(0, 5) == "-----") continue;   // Skip separator

        // Orca Slicer adds "* " to the preset name to indicate that it has been modified
        if (preset_name.substr(0, 2) == "* ") {
            preset_name = preset_name.substr(2);
        }

        const Preset* preset = preset_collection->find_preset(preset_name, false);
        if (preset) {
            // Check if this preset is in the selected preset names set
            bool is_selected = (selected_preset_names.find(preset->name) != selected_preset_names.end());
            presets.push_back({preset, is_selected});
        }
    }

    return PresetsToJson(presets);
}

nlohmann::json PresetConfigUtils::GetEditedPresetJson(Preset::Type type) {
    Tab* tab = wxGetApp().get_tab(type);
    if (!tab) {
        return nlohmann::json::array();
    }
    PresetCollection* presets = tab->get_presets();
    if (!presets) {
        return nlohmann::json::array();
    }

    nlohmann::json j = PresetToJson(&presets->get_edited_preset(), true); // is_selected is true because it is the edited preset

    const bool deep_compare = (type == Preset::TYPE_PRINTER || type == Preset::TYPE_SLA_MATERIAL);
    j["dirty_options"] = presets->current_dirty_options(deep_compare);

    return j;
}


nlohmann::json PresetConfigUtils::GetAllPresetJson() {
    nlohmann::json printerPresetsJson = GetPresetsJson(Preset::Type::TYPE_PRINTER);
    nlohmann::json filamentPresetsJson = GetPresetsJson(Preset::Type::TYPE_FILAMENT);
    nlohmann::json printPresetsJson = GetPresetsJson(Preset::Type::TYPE_PRINT);

    return {
        {"printerPresets", printerPresetsJson},
        {"filamentPresets", filamentPresetsJson},
        {"printProcessPresets", printPresetsJson}
    };
}

nlohmann::json PresetConfigUtils::GetAllEditedPresetJson() {
    nlohmann::json editedPrinterPresetJson = GetEditedPresetJson(Preset::Type::TYPE_PRINTER);
    nlohmann::json editedFilamentPresetJson = GetEditedPresetJson(Preset::Type::TYPE_FILAMENT);
    nlohmann::json editedPrintProcessPresetJson = GetEditedPresetJson(Preset::Type::TYPE_PRINT);

    return {
        {"editedPrinterPreset", editedPrinterPresetJson},
        {"editedFilamentPreset", editedFilamentPresetJson},
        {"editedPrintProcessPreset", editedPrintProcessPresetJson}
    };
}

void PresetConfigUtils::DiscardCurrentPresetChanges() {
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!bundle) {
        return;
    }
    bundle->printers.discard_current_changes();
    bundle->filaments.discard_current_changes();
    bundle->prints.discard_current_changes();
}

void PresetConfigUtils::UpdatePresetTabs() {
    std::array<Preset::Type, 2> preset_types = {Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT};

    for (const auto& preset_type : preset_types) {
        if (Tab* tab = wxGetApp().get_tab(preset_type)) {
            tab->reload_config();
            tab->update();
            tab->update_dirty();
        }
    }
}

/**
 * Apply a config item to the preset
 * @param item: the config item
 */
void PresetConfigUtils::ApplyConfig(const nlohmann::json& item) {
    std::string type = item.value("type", "");
    Preset::Type preset_type;
    if (type == "print") {
        preset_type = Preset::Type::TYPE_PRINT;
    } else if (type == "filament") {
        preset_type = Preset::Type::TYPE_FILAMENT;
    } else {
        std::string error_message = "ApplyConfig: invalid type parameter: " + type;
        wxGetApp().plater()->aiChatPanel()->SendNativeErrorOccurredEvent(error_message);
        BOOST_LOG_TRIVIAL(error) << error_message;
        return;
    }

    Tab* tab = wxGetApp().get_tab(preset_type);
    if (tab != nullptr) {
        try {
            DynamicPrintConfig* config = tab->get_config();
            if (!config) return;

            ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Enable);
            const std::string value_str = item["value"].is_string() ? // Can't blindly dump json object to string, otherwise the original string will become "\"value\""
                item["value"].get<std::string>() : item["value"].dump();
            config->set_deserialize(item.value("key", ""), value_str, context);
            
            // Reload config to update UI controls immediately
            tab->reload_config();
            // Update dirty state to show preset is modified
            tab->update_dirty();
        } catch (const std::exception& e) {
            std::string error_message = "ApplyConfig: '" + item.value("key", "") + ":" + item["value"].dump() + "' failed: " + e.what();
            wxGetApp().plater()->aiChatPanel()->SendNativeErrorOccurredEvent(error_message);
            BOOST_LOG_TRIVIAL(error) << error_message;
            return;
        }
    }
}

void PresetConfigUtils::SelectPreset(const std::string& type, const std::string& presetName) {
    Preset::Type preset_type;
    if (type == "print") {
        preset_type = Preset::Type::TYPE_PRINT;
    } else if (type == "filament") {
        preset_type = Preset::Type::TYPE_FILAMENT;
    } else if (type == "printer") {
        preset_type = Preset::Type::TYPE_PRINTER;
    } else {
        BOOST_LOG_TRIVIAL(error) << "SelectPreset: invalid type parameter";
        throw std::runtime_error("Invalid type parameter");
    }

    DiscardCurrentPresetChanges(); // Selecting a printer will result in selecting a filament or print preset. So we need to discard changes for all presets in order not to have the "transfer or discard" dialog pop up

    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab != nullptr) {
        tab->select_preset(presetName, false, std::string(), false);
    }
}

}} // namespace Slic3r::GUI
