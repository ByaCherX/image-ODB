#pragma once

#include <CLI/CLI.hpp>

namespace image_odb::cli {

/**
 * @brief Registers the 'preview' subcommand with the CLI11 application.
 */
void register_preview_command(CLI::App& app);

} // namespace image_odb::cli
