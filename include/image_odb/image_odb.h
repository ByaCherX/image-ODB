//--------------------------------------------------------------------------+
// Copyright 2026 Emre Kayal                                                |
// Licensed under the Apache License, Version 2.0 (the "License");          |
// you may not use this file except in compliance with the License.         |
// You may obtain a copy of the License at                                  |
//     http://www.apache.org/licenses/LICENSE-2.0                           |
// Unless required by applicable law or agreed to in writing, software      |
// distributed under the License is distributed on an "AS IS" BASIS,        |
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. |
// See the License for the specific language governing permissions and      |
// limitations under the License.                                           |
//--------------------------------------------------------------------------+

#pragma once

#define IMAGE_ODB_VERSION_MAJOR 0
#define IMAGE_ODB_VERSION_MINOR 1
#define IMAGE_ODB_VERSION_PATCH 0
#define IMAGE_ODB_VERSION_STRING "0.1.0"

#include "image_odb/core/types.h"
#include "image_odb/exif_reader.h"
#include "image_odb/image_codec.h"
#include "image_odb/avif_codec.h"
#include "image_odb/jpeg_codec.h"
#include "image_odb/similarity_engine.h"
#include "image_odb/database.h"
#include "image_odb/lru_cache.h"
#include "image_odb/disk_cache.h"
#include "image_odb/cache_manager.h"
#include "image_odb/phash.h"
#include "image_odb/thumbhash.h"
#include "image_odb/logger.h"

#include <functional>
#include <memory>
#include <filesystem>

namespace image_odb {

/**
 * @brief Progress update callback for directory scanning and ingestion.
 */
using ProgressCallback = std::function<void(uint64_t processed, uint64_t total, const std::string& current_file)>;

/**
 * @brief Core engine coordinating database, caching, scanning pipeline, and codecs.
 */
class Engine {
public:
    /**
     * @brief Construct Engine initialized with a working/repository directory.
     * @param working_dir Root directory containing `photos.db` and `.photo_cache/`.
     */
    explicit Engine(std::filesystem::path working_dir);
    ~Engine();

    /**
     * @brief Initialize directory structure, create photos.db and cache if needed.
     */
    bool initialize_workspace();

    /**
     * @brief Scan a directory, extracting metadata, pHash, ThumbHash, and clustering bursts.
     * @param scan_root Path to directory to scan.
     * @param options Scan configuration.
     * @param callback Optional progress reporting callback.
     * @return Number of newly ingested or updated photos.
     */
    uint64_t scan_directory(const std::filesystem::path& scan_root,
                            const ScanOptions& options,
                            ProgressCallback callback = nullptr);

    /**
     * @brief Query photos based on filtering and sorting options.
     */
    std::vector<Photo> list_photos(const ListOptions& options);

    /**
     * @brief Extract a single frame from an AVIF multi-frame file and save to disk.
     */
    bool extract_frame(const std::filesystem::path& avif_path,
                       uint32_t frame_index,
                       const std::filesystem::path& output_path);

    /**
     * @brief Retrieve AVIF thumbnail preview (from memory cache, disk cache, or synthesized on the fly).
     */
    std::optional<ImageBuffer> get_preview(int64_t photo_id);

    /**
     * @brief Clear cache storage.
     * @param memory_only If true, only RAM cache is cleared; otherwise disk cache is also purged.
     */
    void clear_cache(bool memory_only = false);

    /**
     * @brief Convert an individual image file on disk to a target format.
     */
    bool convert_file(const std::filesystem::path& input_path,
                      const std::filesystem::path& output_path,
                      const EncodeOptions& options);

    /**
     * @brief Convert an indexed photo in the database by ID to a target format file.
     */
    bool convert_photo_by_id(int64_t photo_id,
                             const std::filesystem::path& output_path,
                             const EncodeOptions& options);

    /**
     * @brief Convert multiple/all photos in database to target format.
     */
    uint64_t convert_all(const ConvertOptions& options,
                         ProgressCallback callback = nullptr);

    /**
     * @brief Access the underlying database instance.
     */
    [[nodiscard]] db::Database& database() noexcept { return *database_; }

    /**
     * @brief Access the unified two-tier cache manager instance.
     */
    [[nodiscard]] cache::CacheManager& cache_manager() noexcept { return *cache_manager_; }

    /**
     * @brief Access the underlying disk cache instance.
     */
    [[nodiscard]] cache::DiskCache& disk_cache() noexcept { return cache_manager_->disk_cache(); }

    /**
     * @brief Access the in-memory LRU cache instance.
     */
    [[nodiscard]] cache::LruMemoryCache& memory_cache() noexcept { return cache_manager_->memory_cache(); }

private:
    std::filesystem::path working_dir_;
    std::unique_ptr<db::Database> database_;
    std::unique_ptr<cache::CacheManager> cache_manager_;
};

} // namespace image_odb
