#include "image_odb/image_odb.h"
#include "image_odb/core/pipeline.h"
#include <spdlog/spdlog.h>

namespace image_odb {

#define PHOTO_DB "photos.db"
#define PHOTO_CACHE ".photo_cache"

Engine::Engine(std::filesystem::path working_dir)
    : working_dir_(std::move(working_dir)) {
    const auto db_path = working_dir_ / PHOTO_DB;
    const auto cache_dir = working_dir_ / PHOTO_CACHE;

    spdlog::debug("Engine initializing with working directory: '{}', db: '{}', cache: '{}'",
                  working_dir_.string(), db_path.string(), cache_dir.string());

    database_ = std::make_unique<db::Database>(db_path);
    cache_manager_ = std::make_unique<cache::CacheManager>(cache_dir);
}

Engine::~Engine() = default;

bool Engine::initialize_workspace() {
    try {
        if (!std::filesystem::exists(working_dir_)) {
            std::filesystem::create_directories(working_dir_);
        }
        database_->initialize_schema();
        cache_manager_->disk_cache().initialize();
        spdlog::info("Initialized workspace at {}", working_dir_.string());
        return true;
    } catch (const std::exception& ex) {
        spdlog::error("Failed to initialize workspace: {}", ex.what());
        return false;
    }
}

uint64_t Engine::scan_directory(const std::filesystem::path& scan_root,
                                const ScanOptions& options,
                                ProgressCallback callback) {
    core::Pipeline pipeline(*database_, *cache_manager_, working_dir_);
    return pipeline.execute_scan(scan_root, options, callback);
}

std::vector<Photo> Engine::list_photos(const ListOptions& options) {
    return database_->list_photos(options);
}

bool Engine::extract_frame(const std::filesystem::path& avif_path,
                           uint32_t frame_index,
                           const std::filesystem::path& output_path) {
    auto frame = codec::AvifCodec::extract_frame(avif_path, frame_index);
    if (frame.empty()) {
        return false;
    }
    return codec::ImageCodec::encode_file(frame, output_path);
}

std::optional<ImageBuffer> Engine::get_preview(int64_t photo_id, PreviewFormat format) {
    auto photo = database_->find_by_id(photo_id);
    if (!photo.has_value()) {
        return std::nullopt;
    }

    std::filesystem::path full_source = photo->file_path;
    if (!std::filesystem::exists(full_source) && full_source.is_relative()) {
        full_source = working_dir_ / full_source;
    }

    std::string cache_key = photo->hash.empty() ? std::to_string(photo_id) : photo->hash;
    return cache_manager_->get_or_create_preview(cache_key, full_source, format);
}

void Engine::clear_cache(bool memory_only) {
    cache_manager_->purge(memory_only);
    spdlog::info("Cache cleared (memory_only={})", memory_only);
}

} // namespace image_odb
