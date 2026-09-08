#include "cache.h"
#include "image_odb/image_odb.h"
#include <iostream>
#include <string>

namespace image_odb::cli {

namespace {

int handle_cache(const std::string& workspace_dir, bool clear, bool memory_only) {
    Engine engine(workspace_dir);
    if (clear) {
        engine.clear_cache(memory_only);
        std::cout << "Cache cleared successfully (memory_only=" << (memory_only ? "yes" : "no") << ").\n";
    } else {
        auto count = engine.disk_cache().preview_count();
        auto size_bytes = engine.disk_cache().total_size_bytes();
        std::cout << "Cache Location: " << engine.disk_cache().root_path().string() << "\n";
        std::cout << "Cached Previews: " << count << " files\n";
        std::cout << "Total Disk Usage: " << (size_bytes / 1024) << " KB (" << (size_bytes / (1024 * 1024)) << " MB)\n";
    }
    return 0;
}

} // namespace

void register_cache_command(CLI::App& app) {
    auto* cache_cmd = app.add_subcommand("cache", "Inspect or clear the thumbnail preview cache");
    cache_cmd->fallthrough();

    static std::string cache_ws = ".";
    static bool clear_cache = false;
    static bool memory_only = false;

    cache_cmd->add_option("-d,--dir", cache_ws, "Workspace directory")->default_val(".");
    cache_cmd->add_flag("--clear", clear_cache, "Purge cached preview files");
    cache_cmd->add_flag("--memory-only", memory_only, "Clear only in-memory RAM cache");

    cache_cmd->callback([]() {
        return handle_cache(cache_ws, clear_cache, memory_only);
    });
}

} // namespace image_odb::cli
