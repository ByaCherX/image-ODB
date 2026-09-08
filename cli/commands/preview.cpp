#include "preview.h"
#include "common.h"
#include "image_odb/image_odb.h"
#include <iostream>
#include <filesystem>
#include <string>

namespace image_odb::cli {

namespace {

int handle_preview(const std::string& workspace_dir, int64_t photo_id, const std::string& output_file, const std::string& cache_mode_str) {
    Engine engine(workspace_dir);
    if (!std::filesystem::exists(std::filesystem::path(workspace_dir) / "photos.db")) {
        if (!prompt_first_time_database_init(workspace_dir)) {
            return 1;
        }
    }

    if (cache_mode_str == "disk") engine.cache_manager().set_cache_mode(CacheMode::DISK_ONLY);
    else if (cache_mode_str == "ram" || cache_mode_str == "memory") engine.cache_manager().set_cache_mode(CacheMode::RAM_ONLY);
    else if (cache_mode_str == "none" || cache_mode_str == "off") engine.cache_manager().set_cache_mode(CacheMode::NONE);
    else engine.cache_manager().set_cache_mode(CacheMode::ALL);

    auto preview = engine.get_preview(photo_id);
    if (!preview.has_value() || preview->empty()) {
        std::cerr << "Failed to load/generate preview for photo id: " << photo_id << "\n";
        return 1;
    }

    if (!output_file.empty()) {
        EncodeOptions prev_opts;
        prev_opts.format = ImageFormat::AVIF;
        if (codec::ImageCodec::encode_file(*preview, output_file, prev_opts)) {
            std::cout << "Saved preview to: " << output_file << " (" << preview->width << "x" << preview->height << ")\n";
            return 0;
        }
        std::cerr << "Failed to write preview to output file: " << output_file << "\n";
        return 1;
    }

    std::cout << "Preview available for photo id " << photo_id << " (" << preview->width << "x" << preview->height << ", " << preview->size_bytes() << " bytes in RAM)\n";
    return 0;
}

} // namespace

void register_preview_command(CLI::App& app) {
    auto* preview_cmd = app.add_subcommand("preview", "Retrieve or export a preview thumbnail for a photo ID");
    preview_cmd->fallthrough();

    static std::string prev_ws = ".";
    static int64_t prev_id = 0;
    static std::string prev_out = "";
    static std::string prev_cache = "all";

    preview_cmd->add_option("id", prev_id, "Photo ID")->required();
    preview_cmd->add_option("-d,--dir", prev_ws, "Workspace directory")->default_val(".");
    preview_cmd->add_option("-o,--output", prev_out, "Optional path to export preview image file");
    preview_cmd->add_option("--cache", prev_cache, "Cache mode (all, disk, ram, none)")->default_val("all");

    preview_cmd->callback([]() {
        return handle_preview(prev_ws, prev_id, prev_out, prev_cache);
    });
}

} // namespace image_odb::cli
