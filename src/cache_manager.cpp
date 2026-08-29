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
    PreviewFormat format,
    uint32_t max_width,
    uint32_t max_height) {
    
    std::string ram_key = identifier + "_" + (format == PreviewFormat::JPEG ? "jpg" : "avif");

    // Tier 1: Check RAM LRU Cache
    if (auto mem_hit = memory_cache_.get(ram_key); mem_hit.has_value()) {
        spdlog::debug("Cache HIT (RAM): {}", ram_key);
        return mem_hit;
    }

    // Tier 2: Check Disk Preview Cache
    if (disk_cache_.has_preview(identifier, format)) {
        if (auto disk_hit = disk_cache_.load_preview(identifier, format); disk_hit.has_value()) {
            spdlog::debug("Cache HIT (Disk): {}", identifier);
            memory_cache_.put(ram_key, *disk_hit);
            return disk_hit;
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

    // Persist to Disk Cache
    disk_cache_.save_preview(identifier, preview_img, format);

    // Populate RAM Cache
    memory_cache_.put(ram_key, preview_img);

    spdlog::debug("Synthesized & cached new preview: {}", identifier);
    return preview_img;
}

void CacheManager::put_preview(const std::string& identifier,
                               const ImageBuffer& image,
                               PreviewFormat format) {
    if (image.empty()) return;
    std::string ram_key = identifier + "_" + (format == PreviewFormat::JPEG ? "jpg" : "avif");

    disk_cache_.save_preview(identifier, image, format);
    memory_cache_.put(ram_key, image);
}

void CacheManager::purge(bool memory_only) {
    memory_cache_.clear();
    if (!memory_only) {
        disk_cache_.clear();
    }
}

} // namespace image_odb::cache
