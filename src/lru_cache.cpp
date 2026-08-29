#include "image_odb/lru_cache.h"
#include <spdlog/spdlog.h>

namespace image_odb::cache {

LruMemoryCache::LruMemoryCache(size_t max_capacity_bytes)
    : capacity_bytes_(max_capacity_bytes) {}

std::optional<ImageBuffer> LruMemoryCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = items_map_.find(key);
    if (it == items_map_.end()) {
        miss_count_++;
        return std::nullopt;
    }
    hit_count_++;
    // Move to front of LRU list (most recently used)
    items_list_.splice(items_list_.begin(), items_list_, it->second);
    return it->second->buffer;
}

void LruMemoryCache::put(const std::string& key, const ImageBuffer& buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t item_size = buffer.size_bytes();

    auto it = items_map_.find(key);
    if (it != items_map_.end()) {
        current_size_bytes_ -= it->second->size_bytes;
        items_list_.erase(it->second);
        items_map_.erase(it);
    }

    // Evict least recently used entries if over capacity
    while (current_size_bytes_ + item_size > capacity_bytes_ && !items_list_.empty()) {
        auto& last = items_list_.back();
        spdlog::debug("LruMemoryCache: Evicting '{}' ({} bytes)", last.key, last.size_bytes);
        current_size_bytes_ -= last.size_bytes;
        items_map_.erase(last.key);
        items_list_.pop_back();
    }

    if (item_size <= capacity_bytes_) {
        items_list_.push_front(CacheEntry{key, buffer, item_size});
        items_map_[key] = items_list_.begin();
        current_size_bytes_ += item_size;
        spdlog::debug("LruMemoryCache: Stored '{}' ({} bytes, total used: {}/{} bytes, items: {})",
                      key, item_size, current_size_bytes_, capacity_bytes_, items_map_.size());
    } else {
        spdlog::debug("LruMemoryCache: Item '{}' ({} bytes) exceeds total capacity ({} bytes), skipping cache",
                      key, item_size, capacity_bytes_);
    }
}

bool LruMemoryCache::contains(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_map_.find(key) != items_map_.end();
}

bool LruMemoryCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = items_map_.find(key);
    if (it == items_map_.end()) {
        return false;
    }
    current_size_bytes_ -= it->second->size_bytes;
    items_list_.erase(it->second);
    items_map_.erase(it);
    return true;
}

void LruMemoryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    items_list_.clear();
    items_map_.clear();
    current_size_bytes_ = 0;
    spdlog::debug("LruMemoryCache: Cleared all items");
}

size_t LruMemoryCache::current_size_bytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_size_bytes_;
}

size_t LruMemoryCache::capacity_bytes() const noexcept {
    return capacity_bytes_;
}

uint64_t LruMemoryCache::hit_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return hit_count_;
}

uint64_t LruMemoryCache::miss_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return miss_count_;
}

double LruMemoryCache::hit_ratio() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t total = hit_count_ + miss_count_;
    if (total == 0) return 0.0;
    return static_cast<double>(hit_count_) / total;
}

size_t LruMemoryCache::item_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_map_.size();
}

} // namespace image_odb::cache
