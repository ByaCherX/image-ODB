#pragma once

#include <CLI/CLI.hpp>

namespace image_odb::cli {

/**
 * @brief Registers the 'cache' subcommand with the CLI11 application.
 */
void register_cache_command(CLI::App& app);

} // namespace image_odb::cli
