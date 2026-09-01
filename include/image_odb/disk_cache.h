#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <optional>

namespace image_odb::cache {

/**
 * @brief Disk-based AVIF preview thumbnail cache manager.
 */
class DiskCache {
public:
    /**
     * @brief Construct DiskCache with root cache directory.
     * @param cache_root Root directory (e.g., path/to/.photo_cache).
     */
    explicit DiskCache(std::filesystem::path cache_root);

    /**
     * @brief Ensure cache directories exist on disk.
     */
    void initialize();

    /**
     * @brief Get the absolute disk path for a cached AVIF preview file.
     * @param identifier Unique hash or photo ID string.
     * @return Path to the preview file.
     */
    [[nodiscard]] std::filesystem::path get_preview_path(const std::string& identifier) const;

    /**
     * @brief Check if an AVIF preview thumbnail exists on disk.
     * @param identifier Unique hash or photo ID string.
     * @return True if file exists and is non-empty.
     */
    [[nodiscard]] bool has_preview(const std::string& identifier) const;

    /**
     * @brief Save or update an AVIF preview thumbnail on disk.
     * @param identifier Unique hash or photo ID string.
     * @param image Decoded image buffer to encode and save as AVIF.
     * @param quality Quality factor (1-100, default: 80).
     * @return Path where preview was saved, or empty path on failure.
     */
    std::filesystem::path save_preview(const std::string& identifier,
                                       const ImageBuffer& image,
                                       int quality = 80);

    /**
     * @brief Load and decode a cached AVIF preview thumbnail from disk.
     * @param identifier Unique hash or photo ID string.
     * @return Optional ImageBuffer if file was loaded and decoded successfully.
     */
    [[nodiscard]] std::optional<ImageBuffer> load_preview(const std::string& identifier) const;

    /**
     * @brief Delete a specific preview file from disk cache.
     * @param identifier Unique hash or photo ID string.
     * @return True if preview file was removed.
     */
    bool delete_preview(const std::string& identifier);

    /**
     * @brief Remove all preview files from disk cache.
     */
    void clear();

    /**
     * @brief Calculate the total disk space consumed by the cache directory in bytes.
     */
    [[nodiscard]] uint64_t total_size_bytes() const;

    /**
     * @brief Return the total number of cached preview files on disk.
     */
    [[nodiscard]] uint64_t preview_count() const;

    /**
     * @brief Access the root directory of the cache.
     */
    [[nodiscard]] const std::filesystem::path& root_path() const noexcept { return cache_root_; }

private:
    std::filesystem::path cache_root_;
    std::filesystem::path previews_dir_;
};

} // namespace image_odb::cache
