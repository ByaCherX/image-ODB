#include "image_odb/disk_cache.h"
#include "image_odb/image_codec.h"
#include "image_odb/avif_codec.h"
#include "image_odb/jpeg_codec.h"
#include <spdlog/spdlog.h>

namespace image_odb::cache {

DiskCache::DiskCache(std::filesystem::path cache_root)
    : cache_root_(std::move(cache_root)), previews_dir_(cache_root_ / "previews") {}

void DiskCache::initialize() {
    if (!std::filesystem::exists(previews_dir_)) {
        std::filesystem::create_directories(previews_dir_);
        spdlog::debug("DiskCache: Created preview directory '{}'", previews_dir_.string());
    }
}

std::filesystem::path DiskCache::get_preview_path(const std::string& identifier) const {
    return previews_dir_ / (identifier + ".avif");
}

bool DiskCache::has_preview(const std::string& identifier) const {
    auto path = get_preview_path(identifier);
    return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
}

std::filesystem::path DiskCache::save_preview(const std::string& identifier,
                                              const ImageBuffer& image,
                                              int quality) {
    initialize();
    auto target_path = get_preview_path(identifier);
    if (codec::ImageCodec::encode_file(image, target_path, ImageFormat::AVIF, quality)) {
        spdlog::debug("DiskCache: Saved AVIF preview for '{}' to '{}'", identifier, target_path.string());
        return target_path;
    }
    spdlog::warn("DiskCache: Failed to encode/save AVIF preview for '{}' to '{}'", identifier, target_path.string());
    return {};
}

std::optional<ImageBuffer> DiskCache::load_preview(const std::string& identifier) const {
    auto path = get_preview_path(identifier);
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0) {
        return std::nullopt;
    }

    auto img = codec::ImageCodec::decode_file(path);
    if (img.empty()) {
        spdlog::warn("DiskCache: Failed to decode preview from '{}'", path.string());
        return std::nullopt;
    }
    spdlog::debug("DiskCache: Loaded preview for '{}' from '{}'", identifier, path.string());
    return img;
}

bool DiskCache::delete_preview(const std::string& identifier) {
    auto path = get_preview_path(identifier);
    if (std::filesystem::exists(path)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        spdlog::debug("DiskCache: Deleted cached file '{}'", path.string());
        return true;
    }
    return false;
}

void DiskCache::clear() {
    if (std::filesystem::exists(previews_dir_)) {
        std::filesystem::remove_all(previews_dir_);
        initialize();
        spdlog::debug("DiskCache: Cleared all cached preview files");
    }
}

uint64_t DiskCache::total_size_bytes() const {
    uint64_t total = 0;
    if (std::filesystem::exists(previews_dir_)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(previews_dir_)) {
            if (std::filesystem::is_regular_file(entry)) {
                total += std::filesystem::file_size(entry);
            }
        }
    }
    return total;
}

uint64_t DiskCache::preview_count() const {
    uint64_t count = 0;
    if (std::filesystem::exists(previews_dir_)) {
        for (const auto& entry : std::filesystem::directory_iterator(previews_dir_)) {
            if (std::filesystem::is_regular_file(entry)) {
                count++;
            }
        }
    }
    return count;
}

} // namespace image_odb::cache
