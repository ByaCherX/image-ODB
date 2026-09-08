#include "list.h"
#include "common.h"
#include "image_odb/image_odb.h"
#include "image_odb/util.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <string>

namespace image_odb::cli {

namespace {

int handle_list(const std::string& workspace_dir,
                const std::string& sort_by,
                bool ascending,
                const std::string& camera_make,
                const std::string& camera_model,
                const std::string& lens,
                const std::string& location,
                const std::string& from_date,
                const std::string& to_date,
                bool burst_only,
                uint32_t iso_min,
                uint32_t iso_max,
                uint32_t limit,
                uint32_t offset,
                bool output_json) {
    if (!std::filesystem::exists(std::filesystem::path(workspace_dir) / "photos.db")) {
        if (!prompt_first_time_database_init(workspace_dir)) {
            return 1;
        }
    }

    spdlog::debug("CLI handle_list: ws='{}', sort='{}', asc={}, limit={}, offset={}, json={}",
                  workspace_dir, sort_by, ascending, limit, offset, output_json);
    Engine engine(workspace_dir);
    ListOptions options;
    options.sort_by = sort_by;
    options.ascending = ascending;
    if (!camera_make.empty()) options.camera_make_filter = camera_make;
    if (!camera_model.empty()) options.camera_model_filter = camera_model;
    if (!lens.empty()) options.lens_filter = lens;
    if (!location.empty()) options.location_filter = location;
    if (!from_date.empty()) options.date_from = util::parse_datestr(from_date);
    if (!to_date.empty()) options.date_to = util::parse_datestr(to_date, true);
    if (burst_only) options.burst_only = true;
    if (iso_min > 0) options.min_iso = iso_min;
    if (iso_max > 0) options.max_iso = iso_max;
    options.limit = limit;
    options.offset = offset;

    auto photos = engine.list_photos(options);

    if (output_json) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& p : photos) {
            nlohmann::json item;
            item["id"] = p.id;
            item["file_path"] = p.file_path.string();
            item["file_size"] = p.file_size;
            item["hash"] = p.hash;
            item["width"] = p.dimensions.width;
            item["height"] = p.dimensions.height;
            item["mime_type"] = p.mime_type;
            item["is_burst_group"] = p.is_burst_group;
            item["frame_count"] = p.frame_count;
            item["thumbhash"] = p.thumbhash;
            item["phash"] = p.phash;

            if (p.capture_date.has_value()) {
                item["capture_date"] = util::format_iso8601(*p.capture_date);
            }
            if (p.location.latitude.has_value() && p.location.longitude.has_value()) {
                item["location"] = {
                    {"latitude", *p.location.latitude},
                    {"longitude", *p.location.longitude},
                    {"place_name", p.location.place_name}
                };
            }
            item["camera"] = {
                {"make", p.camera.make},
                {"model", p.camera.model}
            };
            item["lens"] = {
                {"model", p.lens.model},
                {"focal_length_mm", p.lens.focal_length_mm.value_or(0.0)}
            };
            item["exposure"] = {
                {"f_number", p.exposure.f_number.value_or(0.0)},
                {"exposure_time", p.exposure.exposure_time_str},
                {"iso_speed", p.exposure.iso_speed.value_or(0)}
            };
            j.push_back(item);
        }
        std::cout << j.dump(2) << "\n";
        return 0;
    }

    std::cout << std::left 
              << std::setw(6)  << "ID"
              << std::setw(12) << "Size"
              << std::setw(10) << "Frames"
              << std::setw(18) << "Camera"
              << std::setw(22) << "Capture Date"
              << "Path" << "\n";
    std::cout << std::string(90, '-') << "\n";

    for (const auto& p : photos) {
        std::string date_str = p.capture_date.has_value() 
            ? util::format_iso8601(*p.capture_date) 
            : "N/A";

        std::string cam_str = p.camera.make.empty() ? "-" : (p.camera.make + " " + p.camera.model);
        if (cam_str.length() > 16) cam_str = cam_str.substr(0, 15) + "...";

        std::cout << std::left
                  << std::setw(6)  << p.id
                  << std::setw(12) << (std::to_string(p.file_size / 1024) + " KB")
                  << std::setw(10) << (p.is_burst_group ? (std::to_string(p.frame_count) + " (Burst)") : "1")
                  << std::setw(18) << cam_str
                  << std::setw(22) << date_str
                  << p.file_path.string() << "\n";
    }
    std::cout << "Total displayed: " << photos.size() << " records\n";
    return 0;
}

} // namespace

void register_list_command(CLI::App& app) {
    auto* list_cmd = app.add_subcommand("list", "Query and list indexed photos with filters");
    list_cmd->fallthrough();

    static std::string list_ws = ".";
    static std::string sort_by = "capture_date";
    static bool ascending = false;
    static std::string camera_make = "";
    static std::string camera_model = "";
    static std::string lens = "";
    static std::string location = "";
    static std::string from_date = "";
    static std::string to_date = "";
    static bool burst_only = false;
    static uint32_t iso_min = 0;
    static uint32_t iso_max = 0;
    static uint32_t limit = 50;
    static uint32_t offset = 0;
    static bool output_json = false;

    list_cmd->add_option("-d,--dir", list_ws, "Workspace directory")->default_val(".");
    list_cmd->add_option("--sort", sort_by, "Sort field (capture_date, created_at, file_size, iso_speed, f_number)")->default_val("capture_date");
    list_cmd->add_flag("--asc", ascending, "Sort in ascending order (default: descending)");
    list_cmd->add_option("--camera-make", camera_make, "Filter by camera make (e.g. Sony, Canon)");
    list_cmd->add_option("--camera-model", camera_model, "Filter by camera model (e.g. ILCE-7M4)");
    list_cmd->add_option("--lens", lens, "Filter by lens model substring");
    list_cmd->add_option("--location", location, "Filter by location substring");
    list_cmd->add_option("--from", from_date, "Filter from capture date (e.g. YYYY-MM-DD or YYYY-MM-DD HH:MM:SS)");
    list_cmd->add_option("--to", to_date, "Filter to capture date (e.g. YYYY-MM-DD or YYYY-MM-DD HH:MM:SS)");
    list_cmd->add_flag("--burst-only", burst_only, "Show only burst container photos");
    list_cmd->add_option("--iso-min", iso_min, "Minimum ISO speed");
    list_cmd->add_option("--iso-max", iso_max, "Maximum ISO speed");
    list_cmd->add_option("--limit", limit, "Maximum records to return")->default_val(50);
    list_cmd->add_option("--offset", offset, "Query pagination offset")->default_val(0);
    list_cmd->add_flag("--json", output_json, "Format output as JSON");

    list_cmd->callback([]() {
        return handle_list(
            list_ws, sort_by, ascending, camera_make, camera_model, lens, location, 
            from_date, to_date, burst_only, iso_min, iso_max, limit, offset, output_json
        );
    });
}

} // namespace image_odb::cli
