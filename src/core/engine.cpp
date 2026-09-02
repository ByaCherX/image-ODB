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

std::optional<ImageBuffer> Engine::get_preview(int64_t photo_id) {
    auto photo = database_->find_by_id(photo_id);
    if (!photo.has_value()) {
        return std::nullopt;
    }

    std::filesystem::path full_source = photo->file_path;
    if (!std::filesystem::exists(full_source) && full_source.is_relative()) {
        full_source = working_dir_ / full_source;
    }

    std::string cache_key = photo->hash.empty() ? std::to_string(photo_id) : photo->hash;
    return cache_manager_->get_or_create_preview(cache_key, full_source);
}

void Engine::clear_cache(bool memory_only) {
    cache_manager_->purge(memory_only);
    spdlog::info("Cache cleared (memory_only={})", memory_only);
}

bool Engine::convert_file(const std::filesystem::path& input_path,
                          const std::filesystem::path& output_path,
                          const EncodeOptions& options) {
    if (!std::filesystem::exists(input_path)) {
        spdlog::error("Engine::convert_file: input file does not exist: {}", input_path.string());
        return false;
    }
    auto img = codec::ImageCodec::decode_file(input_path);
    if (img.empty()) {
        spdlog::error("Engine::convert_file: failed to decode input image: {}", input_path.string());
        return false;
    }
    return codec::ImageCodec::encode_file(img, output_path, options);
}

bool Engine::convert_photo_by_id(int64_t photo_id,
                                 const std::filesystem::path& output_path,
                                 const EncodeOptions& options) {
    auto photo = database_->find_by_id(photo_id);
    if (!photo.has_value()) {
        spdlog::error("Engine::convert_photo_by_id: photo id not found: {}", photo_id);
        return false;
    }
    std::filesystem::path full_source = photo->file_path;
    if (!std::filesystem::exists(full_source) && full_source.is_relative()) {
        full_source = working_dir_ / full_source;
    }
    return convert_file(full_source, output_path, options);
}

uint64_t Engine::convert_all(const ConvertOptions& options,
                             ProgressCallback callback) {
    ListOptions list_opt;
    list_opt.limit = 1000000;
    auto photos = database_->list_photos(list_opt);
    if (photos.empty()) return 0;

    uint64_t total = photos.size();
    uint64_t converted_count = 0;

    for (size_t i = 0; i < total; ++i) {
        const auto& p = photos[i];
        std::filesystem::path src_path = p.file_path;
        if (!std::filesystem::exists(src_path) && src_path.is_relative()) {
            src_path = working_dir_ / src_path;
        }
        if (!std::filesystem::exists(src_path)) continue;

        // Skip if already in desired format
        auto current_fmt = codec::ImageCodec::detect_format(src_path);
        if (options.target_format == current_fmt) {
            if (callback) callback(i + 1, total, src_path.filename().string());
            continue;
        }

        std::filesystem::path out_file;
        std::string ext = (options.target_format == ImageFormat::AVIF) ? ".avif" : (options.target_format == ImageFormat::WEBP ? ".webp" : ".jpg");
        if (!options.output_directory.empty()) {
            if (!std::filesystem::exists(options.output_directory)) {
                std::filesystem::create_directories(options.output_directory);
            }
            out_file = options.output_directory / (src_path.stem().string() + ext);
        } else {
            out_file = src_path.parent_path() / (src_path.stem().string() + ext);
        }

        EncodeOptions enc_opts = options.encode_options;
        enc_opts.format = options.target_format;
        if (convert_file(src_path, out_file, enc_opts)) {
            converted_count++;
            if (options.delete_source && out_file != src_path) {
                std::error_code ec;
                std::filesystem::remove(src_path, ec);
            }
        }

        if (callback) callback(i + 1, total, src_path.filename().string());
    }

    return converted_count;
}

} // namespace image_odb
