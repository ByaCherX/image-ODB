#pragma once

#include <CLI/CLI.hpp>

namespace image_odb::cli {

/**
 * @brief Registers the 'extract' subcommand with the CLI11 application.
 */
void register_extract_command(CLI::App& app);

} // namespace image_odb::cli
