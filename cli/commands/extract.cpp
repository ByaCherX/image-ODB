#include "extract.h"
#include "image_odb/image_odb.h"
#include <iostream>
#include <string>

namespace image_odb::cli {

namespace {

int handle_extract(const std::string& avif_file, uint32_t frame_index, const std::string& output_file) {
    Engine engine(".");
    if (engine.extract_frame(avif_file, frame_index, output_file)) {
        std::cout << "Successfully extracted frame " << frame_index << " from '" 
                  << avif_file << "' -> '" << output_file << "'\n";
        return 0;
    }
    std::cerr << "Failed to extract frame " << frame_index << " from " << avif_file << "\n";
    return 1;
}

} // namespace

void register_extract_command(CLI::App& app) {
    auto* extract_cmd = app.add_subcommand("extract", "Extract a frame from a multi-frame AVIF file");
    extract_cmd->fallthrough();

    static std::string avif_file;
    static uint32_t frame_index = 0;
    static std::string output_file;

    extract_cmd->add_option("avif_file", avif_file, "Path to input .avif container")->required();
    extract_cmd->add_option("-f,--frame", frame_index, "Zero-based frame index to extract")->default_val(0);
    extract_cmd->add_option("-o,--output", output_file, "Output destination image path (.jpg or .avif)")->required();

    extract_cmd->callback([]() {
        return handle_extract(avif_file, frame_index, output_file);
    });
}

} // namespace image_odb::cli
