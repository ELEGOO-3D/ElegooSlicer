#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include <nlohmann/json.hpp>

namespace Slic3r {

class BackgroundSlicingProcess;
class Model;
class Print;
struct GCodeProcessorResult;
struct PrinterNetworkInfo;

namespace GUI {

class PartPlate;
class PartPlateList;

/**
 * @brief RAII timer for measuring operation duration in telemetry events.
 *
 * This class simplifies duration measurement by automatically recording the
 * start time on construction and providing elapsed_ms() to get the duration.
 *
 * Usage example:
 * @code
 * {
 *     TelemetryTimer timer;
 *     // ... perform operation ...
 *     int64_t duration_ms = timer.elapsed_ms();
 *     report_model_import(..., duration_ms, success);
 * }
 * @endcode
 *
 * Or for simple cases:
 * @code
 * TelemetryTimer timer;
 * // ... operation ...
 * report_printer_manual_connect(info, code, timer.elapsed_ms(), success);
 * @endcode
 */
class TelemetryTimer {
public:
    /** @brief Construct timer and record start time. */
    TelemetryTimer() : m_start(std::chrono::steady_clock::now()) {}

    /**
     * @brief Get elapsed time in milliseconds since construction.
     * @return Elapsed time in milliseconds.
     */
    int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start).count();
    }

    /** @brief Reset the timer to current time. */
    void reset() {
        m_start = std::chrono::steady_clock::now();
    }

private:
    std::chrono::steady_clock::time_point m_start;
};

/**
 * @brief Type-safe telemetry event reporting functions.
 *
 * This namespace provides dedicated functions for each telemetry event,
 * encapsulating event names and payload construction logic. This simplifies
 * business layer code by reducing multi-line telemetry calls to single function calls.
 *
 * Usage example:
 * @code
 * // Before (redundant code):
 * TelemetryReporter::getInstance()->reportEvent(
 *     "slice_completed",
 *     TelemetryPayloadBuilder::build_slice_completed_payload(plate, print, result));
 *
 * // After (simplified):
 * TelemetryEvents::report_slice_completed(plate, print, result);
 * @endcode
 */
namespace TelemetryEvents {

/**
 * @brief Reports slice started event and starts timing.
 *
 * This function should be called when slicing begins.
 * It records the start time for duration measurement.
 */
void report_slice_started();

/**
 * @brief Reports app open source event.
 * @param launch_target The launch target URL or empty string for manual launch.
 */
void report_app_open_source(const std::string& launch_target);

/**
 * @brief Reports app launch event.
 */
void report_app_launch();

/**
 * @brief Reports app launch performance event.
 *
 * This function uses the internal timer started by report_app_launch_started().
 */
void report_app_launch_performance();

/**
 * @brief Reports slice completed event.
 *
 * This function uses the internal timer started by report_slice_started().
 * If report_slice_started() was not called, this function will not report.
 *
 * @param plate The completed part plate.
 * @param print The print object.
 * @param result The G-code processor result.
 */
void report_slice_completed(PartPlate& plate, const Print& print,
                           const GCodeProcessorResult& result);

/**
 * @brief Reports slice completed event from BackgroundSlicingProcess.
 *
 * This function automatically extracts plate, print, and result from
 * BackgroundSlicingProcess, with null checks and validation.
 * If report_slice_started() was not called, this function will not report.
 *
 * @param background_process The background slicing process.
 */
void report_slice_completed(BackgroundSlicingProcess& background_process);

/**
 * @brief Reports model import event.
 *
 * This function automatically determines success from imported_object_indices.
 *
 * @param input_files List of imported file paths.
 * @param imported_object_indices Indices of imported objects in the model.
 * @param model The model containing imported objects.
 * @param partplate_list The part plate list.
 * @param import_duration_ms Import duration in milliseconds.
 */
void report_model_import(const std::vector<boost::filesystem::path>& input_files,
                        const std::vector<size_t>& imported_object_indices,
                        const Model& model, PartPlateList& partplate_list,
                        int64_t import_duration_ms);

/**
 * @brief Reports printer manual connect event.
 * @param printer_info Printer network information.
 * @param error_code Error code (0 for success).
 * @param connect_duration_ms Connection duration in milliseconds.
 */
void report_printer_manual_connect(const PrinterNetworkInfo& printer_info,
                                  int error_code, int64_t connect_duration_ms);

/**
 * @brief Reports printer list snapshot event.
 * @param printer_list List of printer network information.
 */
void report_printer_list_snapshot(const std::vector<PrinterNetworkInfo>& printer_list);

/**
 * @brief Reports login click event.
 */
void report_login_click();

/**
 * @brief Reports printer command delivery event.
 * @param content Event content as JSON.
 */
void report_printer_command_delivery(const nlohmann::json& content);

/**
 * @brief Reports print job start event.
 * @param content Event content as JSON (for backward compatibility).
 */
void report_print_job_start(const nlohmann::json& content);

/**
 * @brief Reports print job start event.
 *
 * This function automatically determines print source, network type,
 * and other fields from parameters.
 *
 * @param print_source Print source: "slicer" or "webui".
 * @param printer_info Printer network information.
 * @param error_code Error code (0 for success).
 */
void report_print_job_start(const std::string& print_source,
                           const PrinterNetworkInfo& printer_info,
                           int error_code);

/**
 * @brief Reports printer file transfer event.
 *
 * This function automatically extracts file type from file path,
 * gets file size, and determines result from error code.
 *
 * @param file_path Path to the file being transferred.
 * @param printer_info Printer network information.
 * @param duration_ms Transfer duration in milliseconds.
 * @param error_code Error code (0 for success).
 */
void report_printer_file_transfer(const std::string& file_path,
                                 const PrinterNetworkInfo& printer_info,
                                 int64_t duration_ms,
                                 int error_code);

/**
 * @brief Reports a generic telemetry event.
 *
 * This function is provided for backward compatibility and for events
 * that don't have dedicated reporting functions.
 *
 * @param event_name The event name.
 * @param content Event content as JSON (optional).
 * @param page_name Page name where the event occurred (optional).
 */
void report_event(const std::string& event_name,
                 const nlohmann::json& content = nlohmann::json(),
                 const std::string& page_name = "");

} // namespace TelemetryEvents
} // namespace GUI
} // namespace Slic3r
