#include "TelemetryEvents.hpp"

#include "slic3r/Utils/Elegoo/TelemetryReporter.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrinterNetworkInfo.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"

#include <algorithm>
#include <set>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {
namespace TelemetryEvents {

namespace {
    // Internal state for app launch timing
    TelemetryTimer s_launch_timer;
    bool s_launch_reported{false};
    bool s_app_launch_reported{false};

    // Internal state for slice timing
    bool s_slice_started{false};
    TelemetryTimer s_slice_timer;

    // Helper functions (from TelemetryPayloadBuilder)
    std::vector<size_t> get_used_filament_indices(PartPlate& plate)
    {
        std::vector<size_t> indices;
        for (const int extruder_id : plate.get_used_extruders()) {
            if (extruder_id <= 0) {
                continue;
            }
            indices.push_back(static_cast<size_t>(extruder_id - 1));
        }
        return indices;
    }

    std::string join_used_config_values(const ConfigOptionStrings* option, const std::vector<size_t>& used_indices)
    {
        if (option == nullptr || option->values.empty() || used_indices.empty()) {
            return std::string();
        }
        std::vector<std::string> used_values;
        used_values.reserve(used_indices.size());
        for (const size_t index : used_indices) {
            if (index >= option->values.size()) {
                continue;
            }
            used_values.push_back(option->values[index]);
        }
        if (used_values.empty()) {
            return std::string();
        }
        return boost::algorithm::join(used_values, ";");
    }

    std::string flatten_different_settings_to_system(const DynamicPrintConfig& full_config)
    {
        if (!full_config.has("different_settings_to_system")) {
            return std::string();
        }
        const auto* different_settings = full_config.option<ConfigOptionStrings>("different_settings_to_system");
        if (different_settings == nullptr) {
            return std::string();
        }
        std::vector<std::string> flattened;
        for (const std::string& entry : different_settings->values) {
            if (entry.empty()) {
                continue;
            }
            std::vector<std::string> keys;
            if (Slic3r::unescape_strings_cstyle(entry, keys)) {
                for (const std::string& key : keys) {
                    if (!key.empty() && std::find(flattened.begin(), flattened.end(), key) == flattened.end()) {
                        flattened.push_back(key);
                    }
                }
            } else if (std::find(flattened.begin(), flattened.end(), entry) == flattened.end()) {
                flattened.push_back(entry);
            }
        }
        return boost::algorithm::join(flattened, ";");
    }

    uint64_t get_existing_file_size_bytes(const std::string& path)
    {
        if (path.empty()) {
            return 0;
        }
        boost::system::error_code error_code;
        if (!fs::exists(path, error_code) || error_code) {
            return 0;
        }
        const auto size = fs::file_size(path, error_code);
        return error_code ? 0 : static_cast<uint64_t>(size);
    }

    uint64_t count_plate_triangles(PartPlate& plate, const Model& model)
    {
        uint64_t triangle_count = 0;
        for (const auto& object_and_instance : plate.get_obj_and_inst_set()) {
            const int object_index = object_and_instance.first;
            if (object_index < 0 || static_cast<size_t>(object_index) >= model.objects.size()) {
                continue;
            }
            const ModelObject* model_object = model.objects[object_index];
            if (model_object == nullptr) {
                continue;
            }
            triangle_count += static_cast<uint64_t>(model_object->facets_count());
        }
        return triangle_count;
    }

    std::string build_printer_status(const PrinterNetworkInfo& printer)
    {
        if (printer.connectStatus != PRINTER_CONNECT_STATUS_CONNECTED) {
            return "offline";
        }
        return printer.printerStatus == PRINTER_STATUS_OFFLINE ? "offline" : "online";
    }

    std::string get_query_parameter(const std::string& url, const std::string& key)
    {
        const size_t query_pos = url.find('?');
        if (query_pos == std::string::npos || query_pos + 1 >= url.size()) {
            return std::string();
        }
        const std::string query = url.substr(query_pos + 1);
        size_t token_pos = 0;
        while (token_pos <= query.size()) {
            const size_t next_amp = query.find('&', token_pos);
            const std::string token = query.substr(token_pos, next_amp == std::string::npos ? std::string::npos : next_amp - token_pos);
            const size_t equals_pos = token.find('=');
            const std::string token_key = token.substr(0, equals_pos);
            if (boost::iequals(token_key, key)) {
                if (equals_pos == std::string::npos || equals_pos + 1 >= token.size()) {
                    return std::string();
                }
                const std::string encoded_value = token.substr(equals_pos + 1);
                const std::string decoded_value = Http::url_decode(encoded_value);
                return decoded_value.empty() ? encoded_value : decoded_value;
            }
            if (next_amp == std::string::npos) {
                break;
            }
            token_pos = next_amp + 1;
        }
        return std::string();
    }

    std::string normalize_open_source(std::string source)
    {
        boost::algorithm::trim(source);
        if (source.empty()) {
            return "unknown";
        }
        boost::algorithm::to_lower(source);
        return source;
    }

    std::string normalize_model_type(const fs::path& path)
    {
        std::string extension = path.extension().string();
        boost::algorithm::trim(extension);
        if (extension.empty()) {
            return "unknown";
        }
        if (extension.front() == '.') {
            extension.erase(extension.begin());
        }
        boost::algorithm::to_lower(extension);
        if (extension == "stp") {
            return "step";
        }
        return extension.empty() ? std::string("unknown") : extension;
    }

    std::string build_model_types(const std::vector<fs::path>& input_files)
    {
        std::vector<std::string> model_types;
        model_types.reserve(input_files.size());
        for (const fs::path& input_file : input_files) {
            const std::string normalized_type = normalize_model_type(input_file);
            if (std::find(model_types.begin(), model_types.end(), normalized_type) == model_types.end()) {
                model_types.push_back(normalized_type);
            }
        }
        if (model_types.empty()) {
            return "unknown";
        }
        return boost::algorithm::join(model_types, ";");
    }

    uint64_t get_total_input_file_size_bytes(const std::vector<fs::path>& input_files)
    {
        uint64_t total_size = 0;
        for (const fs::path& input_file : input_files) {
            boost::system::error_code error_code;
            if (!fs::exists(input_file, error_code) || error_code) {
                continue;
            }
            const auto size = fs::file_size(input_file, error_code);
            if (!error_code) {
                total_size += static_cast<uint64_t>(size);
            }
        }
        return total_size;
    }

    std::set<size_t> build_imported_object_index_set(const std::vector<size_t>& imported_object_indices, const Model& model)
    {
        std::set<size_t> imported_object_set;
        for (const size_t object_index : imported_object_indices) {
            if (object_index < model.objects.size() && model.objects[object_index] != nullptr) {
                imported_object_set.insert(object_index);
            }
        }
        return imported_object_set;
    }

    uint64_t count_imported_triangles(const std::set<size_t>& imported_object_indices, const Model& model, uint64_t& max_model_triangle_count)
    {
        uint64_t triangle_count = 0;
        max_model_triangle_count = 0;
        for (const size_t object_index : imported_object_indices) {
            const ModelObject* model_object = model.objects[object_index];
            if (model_object == nullptr) {
                continue;
            }
            const uint64_t object_triangle_count = static_cast<uint64_t>(model_object->facets_count());
            triangle_count += object_triangle_count;
            max_model_triangle_count = std::max(max_model_triangle_count, object_triangle_count);
        }
        return triangle_count;
    }

    size_t count_plates_for_imported_objects(const std::set<size_t>& imported_object_indices, PartPlateList& partplate_list)
    {
        if (imported_object_indices.empty()) {
            return 0;
        }
        size_t plate_count = 0;
        for (int plate_index = 0; plate_index < partplate_list.get_plate_count(); ++plate_index) {
            PartPlate* plate = partplate_list.get_plate(plate_index);
            if (plate == nullptr) {
                continue;
            }
            bool contains_imported_object = false;
            for (const auto& object_and_instance : plate->get_obj_and_inst_set()) {
                if (object_and_instance.first < 0) {
                    continue;
                }
                if (imported_object_indices.count(static_cast<size_t>(object_and_instance.first)) > 0) {
                    contains_imported_object = true;
                    break;
                }
            }
            if (contains_imported_object) {
                ++plate_count;
            }
        }
        return plate_count;
    }
} // namespace

// Slice events
void report_slice_started()
{
    s_slice_started = true;
    s_slice_timer.reset();
}

void report_slice_completed(PartPlate& plate, const Print& print,
                           const GCodeProcessorResult& result)
{
    if (!s_slice_started) {
        return;
    }
    s_slice_started = false;

    const DynamicPrintConfig& full_config = print.full_print_config();
    const std::vector<size_t> used_filament_indices = get_used_filament_indices(plate);
    const auto& normal_time_mode =
        result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    const PrintStatistics& print_statistics = print.print_statistics();
    const ConfigOptionStrings* filament_names = full_config.option<ConfigOptionStrings>("filament_name");
    if (filament_names == nullptr || filament_names->values.empty()) {
        filament_names = full_config.option<ConfigOptionStrings>("filament_settings_id");
    }

    nlohmann::json payload = nlohmann::json::object();
    payload["printer_model"] = full_config.has("printer_model") ? full_config.opt_string("printer_model") : std::string();
    payload["file_size_bytes"] = get_existing_file_size_bytes(plate.get_tmp_gcode_path());
    payload["layer_count"] = normal_time_mode.layers_times.size();
    payload["estimated_time_sec"] = normal_time_mode.time > 0.0f ? static_cast<int64_t>(normal_time_mode.time + 0.5f) : 0;
    payload["total_filament_used_g"] = print_statistics.total_weight;
    payload["total_filament_used_mm"] = print_statistics.total_used_filament;
    payload["filament_colour"] = join_used_config_values(full_config.option<ConfigOptionStrings>("filament_colour"), used_filament_indices);
    payload["filament_type"] = join_used_config_values(full_config.option<ConfigOptionStrings>("filament_type"), used_filament_indices);
    payload["filament_name"] = join_used_config_values(filament_names, used_filament_indices);
    payload["different_settings_to_system"] = flatten_different_settings_to_system(full_config);
    payload["triangle_count"] = count_plate_triangles(plate, print.model());
    payload["duration_ms"] = std::max<int64_t>(s_slice_timer.elapsed_ms(), 0);

    TelemetryReporter::getInstance()->reportEvent("slice_completed", payload);
}

void report_slice_completed(BackgroundSlicingProcess& background_process)
{
    PartPlate* current_plate = background_process.get_current_plate();
    Print* current_print = background_process.fff_print();
    GCodeProcessorResult* current_slice_result = current_plate ? current_plate->get_slice_result() : nullptr;

    if (current_plate != nullptr && current_print != nullptr && current_slice_result != nullptr &&
        current_plate->has_printable_instances() && current_plate->is_slice_result_valid()) {
        report_slice_completed(*current_plate, *current_print, *current_slice_result);
    }
}

// App events
void report_app_launch_performance()
{
    if (s_launch_reported) {
        return;
    }
    s_launch_reported = true;
    nlohmann::json content = nlohmann::json::object();
    content["cold_start_ms"] = std::max<int64_t>(s_launch_timer.elapsed_ms(), 0);
    TelemetryReporter::getInstance()->reportEvent("app_launch_performance", content);
}

void report_app_launch()
{
    if (s_app_launch_reported) {
        return;
    }
    s_app_launch_reported = true;
    TelemetryReporter::getInstance()->reportEvent("app_launch");
}

void report_app_open_source(const std::string& launch_target)
{
    const bool is_third_party_launch = !launch_target.empty() && is_supported_open_protocol(launch_target);

    nlohmann::json payload = nlohmann::json::object();
    payload["open_type"] = is_third_party_launch ? "third_party" : "manual";
    payload["source"] = is_third_party_launch ? normalize_open_source(get_query_parameter(launch_target, "source")) : "";
    TelemetryReporter::getInstance()->reportEvent("app_open_source", payload);
}

// Model events
void report_model_import(const std::vector<boost::filesystem::path>& input_files,
                        const std::vector<size_t>& imported_object_indices,
                        const Model& model, PartPlateList& partplate_list,
                        int64_t import_duration_ms)
{
    const bool success = !imported_object_indices.empty();
    const std::set<size_t> imported_object_set = build_imported_object_index_set(imported_object_indices, model);
    uint64_t max_model_triangle_count = 0;

    nlohmann::json payload = nlohmann::json::object();
    payload["result"] = success ? "success" : "fail";
    payload["model_type"] = build_model_types(input_files);
    payload["model_size_bytes"] = get_total_input_file_size_bytes(input_files);
    payload["model_count"] = imported_object_set.size();
    payload["plate_count"] = count_plates_for_imported_objects(imported_object_set, partplate_list);
    payload["triangle_count"] = count_imported_triangles(imported_object_set, model, max_model_triangle_count);
    payload["max_model_triangle_count"] = max_model_triangle_count;
    payload["duration_ms"] = std::max<int64_t>(import_duration_ms, 0);
    TelemetryReporter::getInstance()->reportEvent("model_import", payload);
}

// Printer events
void report_printer_manual_connect(const PrinterNetworkInfo& printer_info,
                                  int error_code, int64_t connect_duration_ms)
{
    nlohmann::json payload = nlohmann::json::object();
    payload["result"] = (error_code == 0) ? "success" : "fail";
    payload["network_type"] = (printer_info.networkType == NETWORK_TYPE_WAN) ? "wan" : "lan";
    payload["printer_model"] = printer_info.printerModel;
    payload["serial_number"] = printer_info.serialNumber;
    payload["error_code"] = error_code;
    payload["connect_duration_ms"] = std::max<int64_t>(connect_duration_ms, 0);
    TelemetryReporter::getInstance()->reportEvent("printer_manual_connect", payload);
}

void report_printer_list_snapshot(const std::vector<PrinterNetworkInfo>& printer_list)
{
    nlohmann::json payload = nlohmann::json::object();
    payload["printer_count"] = printer_list.size();
    payload["printers"] = nlohmann::json::array();
    for (const PrinterNetworkInfo& printer : printer_list) {
        nlohmann::json printer_json = nlohmann::json::object();
        printer_json["printer_model"] = printer.printerModel;
        printer_json["serial_number"] = printer.serialNumber;
        printer_json["network_type"] = printer.networkType == NETWORK_TYPE_WAN ? "wan" : "lan";
        printer_json["firmware_version"] = printer.firmwareVersion;
        printer_json["status"] = build_printer_status(printer);
        payload["printers"].push_back(std::move(printer_json));
    }
    TelemetryReporter::getInstance()->reportEvent("printer_list_snapshot", payload);
}

void report_printer_file_transfer(const std::string& file_path,
                                 const PrinterNetworkInfo& printer_info,
                                 int64_t duration_ms,
                                 int error_code)
{
    const std::string result = (error_code == 0) ? "success" : "fail";
    const std::string network_type = (printer_info.networkType == NETWORK_TYPE_WAN) ? "wan" : "lan";

    std::string file_type = "gcode";
    if (file_path.size() >= 4) {
        std::string ext = file_path.substr(file_path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".3mf") {
            file_type = "3mf";
        }
    }

    uint64_t file_size_bytes = get_existing_file_size_bytes(file_path);

    nlohmann::json payload = nlohmann::json::object();
    payload["result"] = result;
    payload["network_type"] = network_type;
    payload["printer_model"] = printer_info.printerModel;
    payload["serial_number"] = printer_info.serialNumber;
    payload["file_type"] = file_type;
    payload["file_size_bytes"] = file_size_bytes;
    payload["duration_ms"] = std::max<int64_t>(duration_ms, 0);
    payload["error_code"] = error_code;
    TelemetryReporter::getInstance()->reportEvent("printer_file_transfer", payload);
}

void report_print_job_start(const std::string& print_source,
                           const PrinterNetworkInfo& printer_info,
                           int error_code)
{
    nlohmann::json payload = nlohmann::json::object();
    payload["result"] = (error_code == 0) ? "success" : "fail";
    payload["print_source"] = print_source;
    payload["network_type"] = (printer_info.networkType == NETWORK_TYPE_WAN) ? "wan" : "lan";
    payload["printer_model"] = printer_info.printerModel;
    payload["serial_number"] = printer_info.serialNumber;
    payload["error_code"] = error_code;
    TelemetryReporter::getInstance()->reportEvent("print_job_start", payload);
}

// User events
void report_login_click()
{
    TelemetryReporter::getInstance()->reportEvent("login_click");
}

// IPC events (backward compatibility)
void report_printer_command_delivery(const nlohmann::json& content)
{
    TelemetryReporter::getInstance()->reportEvent("printer_command_delivery", content);
}

void report_print_job_start(const nlohmann::json& content)
{
    TelemetryReporter::getInstance()->reportEvent("print_job_start", content);
}

// Generic event
void report_event(const std::string& event_name,
                 const nlohmann::json& content,
                 const std::string& page_name)
{
    TelemetryReporter::getInstance()->reportEvent(event_name, content, page_name);
}

} // namespace TelemetryEvents
} // namespace GUI
} // namespace Slic3r
