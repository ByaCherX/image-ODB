#include "image_odb/cache_manager.h"
#include "image_odb/image_codec.h"
#include <spdlog/spdlog.h>

namespace image_odb::cache {

CacheManager::CacheManager(std::filesystem::path cache_root, size_t ram_limit_bytes)
    : disk_cache_(std::move(cache_root)), memory_cache_(ram_limit_bytes) {
    disk_cache_.initialize();
}

std::optional<ImageBuffer> CacheManager::get_or_create_preview(
    const std::string& identifier,
    const std::filesystem::path& source_file,
    uint32_t max_width,
    uint32_t max_height) {
    
    // Tier 1: Check RAM LRU Cache (if enabled)
    if (cache_mode_ == CacheMode::ALL || cache_mode_ == CacheMode::RAM_ONLY) {
        if (auto mem_hit = memory_cache_.get(identifier); mem_hit.has_value()) {
            spdlog::debug("Cache HIT (RAM): {}", identifier);
            return mem_hit;
        }
    }

    // Tier 2: Check Disk Preview Cache (if enabled)
    if (cache_mode_ == CacheMode::ALL || cache_mode_ == CacheMode::DISK_ONLY) {
        if (disk_cache_.has_preview(identifier)) {
            if (auto disk_hit = disk_cache_.load_preview(identifier); disk_hit.has_value()) {
                spdlog::debug("Cache HIT (Disk): {}", identifier);
                if (cache_mode_ == CacheMode::ALL) {
                    memory_cache_.put(identifier, *disk_hit);
                }
                return disk_hit;
            }
        }
    }

    // Tier 3: Synthesize preview from original source file
    if (!std::filesystem::exists(source_file)) {
        spdlog::warn("Source file does not exist for preview synthesis: {}", source_file.string());
        return std::nullopt;
    }

    auto full_img = codec::ImageCodec::decode_file(source_file);
    if (full_img.empty()) {
        spdlog::warn("Failed to decode source image for preview: {}", source_file.string());
        return std::nullopt;
    }

    // Resize to thumbnail dimensions
    auto preview_img = codec::ImageCodec::resize_aspect_fit(full_img, max_width, max_height);
    if (preview_img.empty()) {
        preview_img = std::move(full_img);
    }

    // Persist to Disk Cache (if enabled)
    if (cache_mode_ == CacheMode::ALL || cache_mode_ == CacheMode::DISK_ONLY) {
        disk_cache_.save_preview(identifier, preview_img);
    }

    // Populate RAM Cache (if enabled)
    if (cache_mode_ == CacheMode::ALL || cache_mode_ == CacheMode::RAM_ONLY) {
        memory_cache_.put(identifier, preview_img);
    }

    spdlog::debug("Synthesized AVIF preview (cache_mode={}): {}", static_cast<int>(cache_mode_), identifier);
    return preview_img;
}

void CacheManager::put_preview(const std::string& identifier,
                               const ImageBuffer& image) {
    if (image.empty() || cache_mode_ == CacheMode::NONE) return;

    if (cache_mode_ == CacheMode::ALL || cache_mode_ == CacheMode::DISK_ONLY) {
        disk_cache_.save_preview(identifier, image);
    }
    if (cache_mode_ == CacheMode::ALL || cache_mode_ == CacheMode::RAM_ONLY) {
        memory_cache_.put(identifier, image);
    }
}

void CacheManager::purge(bool memory_only) {
    memory_cache_.clear();
    if (!memory_only) {
        disk_cache_.clear();
    }
}

} // namespace image_odb::cache
