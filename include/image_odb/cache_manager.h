#pragma once

#include "image_odb/core/types.h"
#include "image_odb/lru_cache.h"
#include "image_odb/disk_cache.h"
#include <filesystem>
#include <memory>

namespace image_odb::cache {

/**
 * @brief Coordinates Two-Tier Caching (RAM LRU Cache + Disk Thumbnail Cache).
 */
class CacheManager {
public:
    /**
     * @brief Construct CacheManager with cache root directory and RAM limit.
     * @param cache_root Disk path (e.g. .photo_cache).
     * @param ram_limit_bytes Maximum memory for decoded preview bitmaps (default: 512 MB).
     */
    explicit CacheManager(std::filesystem::path cache_root, size_t ram_limit_bytes = 512 * 1024 * 1024);
    ~CacheManager() = default;

    /**
     * @brief Retrieve preview from RAM, Disk, or synthesize from source on the fly.
     * @param identifier Unique cache key (e.g., photo ID string or hash).
     * @param source_file Original image file path on disk (used to decode if preview doesn't exist).
     * @param format Desired preview format (AVIF or JPEG).
     * @param max_width Thumbnail max width (default: 500).
     * @param max_height Thumbnail max height (default: 500).
     * @return Decoded preview ImageBuffer.
     */
    std::optional<ImageBuffer> get_or_create_preview(
        const std::string& identifier,
        const std::filesystem::path& source_file,
        PreviewFormat format = PreviewFormat::AVIF,
        uint32_t max_width = 500,
        uint32_t max_height = 500
    );

    /**
     * @brief Store an explicit preview in both RAM and Disk cache.
     * @param identifier Unique cache key.
     * @param image Preview image buffer.
     * @param format Preview format.
     */
    void put_preview(const std::string& identifier,
                     const ImageBuffer& image,
                     PreviewFormat format = PreviewFormat::AVIF);

    /**
     * @brief Purge cache storage.
     * @param memory_only If true, purges only RAM LRU cache; otherwise clears both RAM and Disk cache.
     */
    void purge(bool memory_only = false);

    /**
     * @brief Access the in-memory LRU cache.
     */
    [[nodiscard]] LruMemoryCache& memory_cache() noexcept { return memory_cache_; }

    /**
     * @brief Access the disk preview cache.
     */
    [[nodiscard]] DiskCache& disk_cache() noexcept { return disk_cache_; }

private:
    DiskCache disk_cache_;
    LruMemoryCache memory_cache_;
};

} // namespace image_odb::cache
