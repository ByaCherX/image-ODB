#pragma once

#include <CLI/CLI.hpp>

namespace image_odb::cli {

/**
 * @brief Registers the 'scan' subcommand with the CLI11 application.
 */
void register_scan_command(CLI::App& app);

} // namespace image_odb::cli
