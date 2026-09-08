#pragma once

#include <string>

namespace image_odb::cli {

/**
 * @brief Prompts user to automatically initialize and scan the workspace if photos.db is missing.
 * @param workspace_dir Workspace root directory.
 * @return True if database exists or was successfully initialized, false if cancelled or failed.
 */
bool prompt_first_time_database_init(const std::string& workspace_dir);

} // namespace image_odb::cli
