#include "commands/init.h"
#include "commands/image.h"
#include "commands/scan.h"
#include "commands/list.h"
#include "commands/extract.h"
#include "commands/preview.h"
#include "image_odb/image_odb.h"
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    // Ensure Windows console renders UTF-8 properly and supports ANSI escape codes
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif

    // Initialize default colored console logging at INFO level
    image_odb::Logger::init();

    CLI::App app{"image_cli: High-performance photo database and AVIF multi-frame archiving CLI tool"};
    app.set_version_flag("--version", image_odb_version);
    app.fallthrough();
    app.require_subcommand(1);

    std::string log_level_str = "info";
    app.add_option("--loglevel", log_level_str, "Set logging level (trace, debug, info, warn, error, critical, off)")
        ->default_val("info")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}, CLI::ignore_case));

    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Enable verbose debug logging (equivalent to --loglevel=debug)");

    // Configure logger before subcommands execute
    app.callback([&]() {
        if (verbose) log_level_str = "debug";
        image_odb::Logger::configure_with(log_level_str);
    });

    // Register modular subcommands directly
    image_odb::cli::register_init_command(app);
    image_odb::cli::register_image_command(app);
    image_odb::cli::register_scan_command(app);
    image_odb::cli::register_list_command(app);
    image_odb::cli::register_extract_command(app);
    image_odb::cli::register_preview_command(app);

    // If no arguments are provided, redirect directly to help output
    if (argc == 1) {
        return app.exit(CLI::CallForHelp());
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::RequiredError& e) {
        // If a required subcommand is missing, display help instead of a generic error
        bool any_subcommand = false;
        for (const auto* sub : app.get_subcommands()) {
            if (sub->parsed()) {
                any_subcommand = true;
                break;
            }
        }
        if (!any_subcommand) {
            return app.exit(CLI::CallForHelp());
        }
        return app.exit(e);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
