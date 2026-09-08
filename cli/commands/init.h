#pragma once

#include <CLI/CLI.hpp>

namespace image_odb::cli {

/**
 * @brief Registers the 'init' subcommand with the CLI11 application.
 */
void register_init_command(CLI::App& app);

} // namespace image_odb::cli
