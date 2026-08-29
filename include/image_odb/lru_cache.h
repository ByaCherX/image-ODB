#pragma once

#include "image_odb/core/types.h"
#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <optional>
#include <memory>
#include <atomic>

namespace image_odb::cache {

/**
 * @brief Thread-safe Least-Recently-Used (LRU) cache for decoded ImageBuffer objects with memory size limit and hit statistics.
 */
class LruMemoryCache {
public:
    /**
     * @brief Construct LRU cache with a maximum capacity in bytes.
     * @param max_capacity_bytes Memory limit in bytes (default: 512 MB).
     */
    explicit LruMemoryCache(size_t max_capacity_bytes = 512 * 1024 * 1024);
    ~LruMemoryCache() = default;

    /**
     * @brief Retrieve an image from cache.
     * @param key Unique cache key (e.g., file_path or hash).
     * @return Optional copy/shared reference of ImageBuffer if found.
     */
    std::optional<ImageBuffer> get(const std::string& key);

    /**
     * @brief Insert or update an image in cache, evicting oldest entries if memory limit is exceeded.
     * @param key Unique cache key.
     * @param buffer Image data buffer.
     */
    void put(const std::string& key, const ImageBuffer& buffer);

    /**
     * @brief Check if a key exists in cache without updating its LRU position.
     * @param key Unique cache key.
     * @return True if key is present.
     */
    [[nodiscard]] bool contains(const std::string& key) const;

    /**
     * @brief Remove a specific entry from cache.
     * @param key Unique cache key.
     * @return True if key was found and removed.
     */
    bool remove(const std::string& key);

    /**
     * @brief Clear all cached items in memory and reset memory counters.
     */
    void clear();

    /**
     * @brief Current total memory consumption in bytes.
     */
    [[nodiscard]] size_t current_size_bytes() const noexcept;

    /**
     * @brief Maximum capacity in bytes.
     */
    [[nodiscard]] size_t capacity_bytes() const noexcept;

    /**
     * @brief Total number of successful cache hits.
     */
    [[nodiscard]] uint64_t hit_count() const noexcept;

    /**
     * @brief Total number of cache lookup misses.
     */
    [[nodiscard]] uint64_t miss_count() const noexcept;

    /**
     * @brief Ratio of hits over total lookups (0.0 to 1.0).
     */
    [[nodiscard]] double hit_ratio() const noexcept;

    /**
     * @brief Total number of currently cached items.
     */
    [[nodiscard]] size_t item_count() const noexcept;

private:
    struct CacheEntry {
        std::string key;
        ImageBuffer buffer;
        size_t size_bytes{0};
    };

    size_t capacity_bytes_;
    size_t current_size_bytes_{0};
    mutable std::mutex mutex_;
    mutable uint64_t hit_count_{0};
    mutable uint64_t miss_count_{0};
    std::list<CacheEntry> items_list_;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> items_map_;
};

} // namespace image_odb::cache
