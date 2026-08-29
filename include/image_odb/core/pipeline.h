#pragma once

#include "image_odb/core/types.h"
#include "image_odb/database.h"
#include "image_odb/cache_manager.h"
#include <filesystem>
#include <functional>
#include <vector>

namespace image_odb::core {

using ProgressCallback = std::function<void(uint64_t processed, uint64_t total, const std::string& current_file)>;

/**
 * @brief High-throughput multi-threaded directory scanner and ingestion pipeline.
 */
class Pipeline {
public:
    Pipeline(db::Database& database, cache::CacheManager& cache_manager, std::filesystem::path working_dir);
    ~Pipeline() = default;

    /**
     * @brief Scan and ingest all images in scan_root directory.
     * @param scan_root Path to directory to scan.
     * @param options Configuration options.
     * @param callback Progress reporting callback.
     * @return Number of ingested photo records.
     */
    uint64_t execute_scan(const std::filesystem::path& scan_root,
                          const ScanOptions& options,
                          ProgressCallback callback = nullptr);

    /**
     * @brief Compute BLAKE3 content hash hex string for a file on disk.
     */
    static std::string compute_hash(const std::filesystem::path& file_path);

private:
    db::Database& db_;
    cache::CacheManager& cache_mgr_;
    std::filesystem::path working_dir_;
};

} // namespace image_odb::core
