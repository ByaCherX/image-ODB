#include "init.h"
#include "image_odb/image_odb.h"
#include <iostream>
#include <filesystem>
#include <string>

namespace image_odb::cli {

namespace {

int handle_init(const std::string& directory) {
    Engine engine(directory);
    if (engine.initialize_workspace()) {
        std::cout << "Successfully initialized workspace at: " << std::filesystem::absolute(directory).string() << "\n";
        return 0;
    }
    std::cerr << "Error initializing workspace at: " << directory << "\n";
    return 1;
}

} // namespace

void register_init_command(CLI::App& app) {
    auto* init_cmd = app.add_subcommand("init", "Initialize database (photos.db)");
    init_cmd->fallthrough();

    static std::string init_dir = ".";
    init_cmd->add_option("-d,--dir", init_dir, "Target workspace directory")->default_val(".");
    init_cmd->callback([]() {
        return handle_init(init_dir);
    });
}

} // namespace image_odb::cli
