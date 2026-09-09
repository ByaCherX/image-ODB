#include "common.h"
#include "image_odb/image_odb.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace image_odb::cli {

bool prompt_first_time_database_init(const std::string& workspace_dir) {
    std::filesystem::path ws(workspace_dir);
    std::filesystem::path db_file = ws / "photos.db";

    if (std::filesystem::exists(db_file)) {
        return true;
    }

    std::cout << "\n";
    std::cout << "+-------------------------------------------------------------------------+\n";
    std::cout << "|  Database (photos.db) not found in this directory.                      |\n";
    std::cout << "|  It looks like image_cli is running in this workspace for the first     |\n";
    std::cout << "|  time. Initialize database and scan directory automatically? [Y/n]      |\n";
    std::cout << "+-------------------------------------------------------------------------+\n";
    std::cout << "> ";
    std::cout.flush();

    std::string response;
    if (!std::getline(std::cin, response)) {
        return false;
    }

    while (!response.empty() && (response.front() == ' ' || response.front() == '\t')) response.erase(0, 1);
    while (!response.empty() && (response.back() == ' ' || response.back() == '\t' || response.back() == '\r')) response.pop_back();

    if (response.empty() || response == "y" || response == "Y" || response == "yes") {
        std::cout << "Initializing database and scanning directory...\n";
        Engine engine(workspace_dir);
        if (!engine.initialize_workspace()) {
            std::cerr << "Failed to initialize workspace.\n";
            return false;
        }
        ScanOptions scan_opt;
        scan_opt.recursive = true;
        uint64_t count = engine.scan_directory(workspace_dir, scan_opt, [](uint64_t processed, uint64_t total, const std::string& cur) {
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
        });
        std::cout << "\x1b[2K\rScanning finished! " << count << " images indexed.\n\n";
        return true;
    }

    std::cout << "Operation cancelled.\n";
    return false;
}

} // namespace image_odb::cli
