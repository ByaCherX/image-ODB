#include "scan.h"
#include "image_odb/image_odb.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

namespace image_odb::cli {

namespace {

int handle_scan(const std::string& workspace_dir,
                const std::string& scan_dir,
                bool group_bursts,
                uint32_t threads,
                bool recursive,
                uint32_t burst_time,
                uint32_t burst_dist,
                bool convert_to_avif,
                int convert_quality,
                int convert_speed,
                bool convert_lossless,
                const std::string& convert_subsampling,
                int convert_depth,
                const std::string& convert_out_dir,
                bool delete_source,
                bool embed_thumb) {
    Engine engine(workspace_dir);
    engine.initialize_workspace();

    ScanOptions options;
    options.group_bursts = group_bursts;
    options.recursive = recursive;
    options.max_threads = threads;
    options.burst_time_window_seconds = burst_time;
    options.burst_max_hamming_distance = burst_dist;

    // Convert options
    options.convert_to_avif = convert_to_avif;
    if (convert_to_avif) {
        options.convert_options.format = ImageFormat::AVIF;
        options.convert_options.quality = convert_quality;
        options.convert_options.speed = convert_speed;
        options.convert_options.lossless = convert_lossless;
        options.convert_options.bit_depth = convert_depth;
        options.convert_options.embed_thumbnail = embed_thumb;

        if (convert_subsampling == "444") options.convert_options.subsampling = ChromaSubsampling::YUV444;
        else if (convert_subsampling == "422") options.convert_options.subsampling = ChromaSubsampling::YUV422;
        else if (convert_subsampling == "400") options.convert_options.subsampling = ChromaSubsampling::YUV400;
        else options.convert_options.subsampling = ChromaSubsampling::YUV420;

        if (!convert_out_dir.empty()) {
            options.convert_output_dir = convert_out_dir;
        }
        options.delete_source = delete_source;
    }

    std::cout << "Scanning directory: " << scan_dir << "\n";
    std::cout << "Parameters: [threads=" << (threads == 0 ? "auto" : std::to_string(threads))
              << ", group_bursts=" << (group_bursts ? "yes" : "no")
              << ", convert=" << (convert_to_avif ? "yes" : "no") << "]\n";

    uint64_t count = engine.scan_directory(scan_dir, options, [](uint64_t processed, uint64_t total, const std::string& cur) {
        if (Logger::get_level() > spdlog::level::debug) {
            int percent = (total > 0) ? static_cast<int>((processed * 100) / total) : 0;
            std::string display_name = cur;
            constexpr size_t max_name_len = 35;
            if (display_name.length() > max_name_len) {
                display_name = display_name.substr(0, max_name_len - 3) + "...";
            }

            std::ostringstream oss;
            oss << "[" << std::setw(3) << percent << "%] [" 
                << processed << "/" << total << "] " << display_name;
            std::string line = oss.str();

            constexpr size_t line_width = 79;
            if (line.length() < line_width) {
                line.append(line_width - line.length(), ' ');
            }

            std::cout << "\x1b[2K\r" << line << std::flush;
        }
    });

    if (Logger::get_level() > spdlog::level::debug) {
        std::cout << "\x1b[2K\r" << std::flush;
    }
    std::cout << "Scanning finished! Total new/updated items ingested: " << count << "\n";
    return 0;
}

} // namespace

void register_scan_command(CLI::App& app) {
    auto* scan_cmd = app.add_subcommand("scan", "Scan directory for photos, extract EXIF/hashes, and ingest");
    scan_cmd->fallthrough();

    static std::string scan_dir = ".";
    static std::string scan_ws = "";
    static bool group_bursts = false;
    static uint32_t threads = 0;
    static bool no_recursive = false;
    static uint32_t burst_time = 3;
    static uint32_t burst_dist = 5;
    static bool scan_convert = false;
    static int scan_conv_quality = 80;
    static int scan_conv_speed = 6;
    static bool scan_conv_lossless = false;
    static std::string scan_conv_subsampling = "420";
    static int scan_conv_depth = 8;
    static std::string scan_conv_out_dir = "";
    static bool scan_delete_source = false;
    static bool scan_embed_thumb = false;

    scan_cmd->add_option("-d,--dir", scan_dir, "Directory containing photos to scan")->default_val(".");
    scan_cmd->add_option("-w,--workspace", scan_ws, "Workspace directory for database (default: same as -d)");
    scan_cmd->add_flag("--group-bursts", group_bursts, "Detect and group burst shots into multi-frame AVIF");
    scan_cmd->add_option("-t,--threads", threads, "Number of worker threads (0 = auto)")->default_val(0);
    scan_cmd->add_flag("--no-recursive", no_recursive, "Do not scan subdirectories recursively");
    scan_cmd->add_option("--burst-time", burst_time, "Max time difference between burst shots (seconds)")->default_val(3);
    scan_cmd->add_option("--burst-dist", burst_dist, "Max pHash Hamming distance for burst grouping")->default_val(5);
    scan_cmd->add_flag("--convert", scan_convert, "Convert scanned non-AVIF images to AVIF format");
    scan_cmd->add_option("--convert-quality", scan_conv_quality, "AVIF conversion quality (1-100)")->default_val(80);
    scan_cmd->add_option("--convert-speed", scan_conv_speed, "AVIF conversion speed (0-10)")->default_val(6);
    scan_cmd->add_flag("--convert-lossless", scan_conv_lossless, "Enable lossless AVIF conversion");
    scan_cmd->add_option("--convert-subsampling", scan_conv_subsampling, "Conversion chroma subsampling (420, 422, 444, 400)")->default_val("420");
    scan_cmd->add_option("--convert-depth", scan_conv_depth, "Conversion bit depth (8, 10, 12)")->default_val(8);
    scan_cmd->add_option("--convert-out-dir", scan_conv_out_dir, "Destination directory for converted AVIFs");
    scan_cmd->add_flag("--delete-source", scan_delete_source, "Delete source file after converting to AVIF");
    scan_cmd->add_flag("--embed-thumb", scan_embed_thumb, "Embed downscaled thumbnail inside converted AVIF");

    scan_cmd->callback([]() {
        const std::string& actual_ws = scan_ws.empty() ? scan_dir : scan_ws;
        return handle_scan(actual_ws, scan_dir, group_bursts, threads, !no_recursive,
                           burst_time, burst_dist, scan_convert, scan_conv_quality, scan_conv_speed, scan_conv_lossless,
                           scan_conv_subsampling, scan_conv_depth, scan_conv_out_dir, scan_delete_source, scan_embed_thumb);
    });
}

} // namespace image_odb::cli
