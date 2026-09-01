#include "image_odb/core/pipeline.h"
#include "image_odb/exif_reader.h"
#include "image_odb/image_codec.h"
#include "image_odb/avif_codec.h"
#include "image_odb/phash.h"
#include "image_odb/thumbhash.h"
#include "image_odb/similarity_engine.h"
#include <spdlog/spdlog.h>
#include <blake3.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <future>
#include <mutex>
#include <algorithm>
#include <unordered_set>

namespace image_odb::core {

namespace {

bool is_supported_image(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".avif" || ext == ".png" || ext == ".webp");
}

} // namespace

std::string Pipeline::compute_hash(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return "";

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    char buffer[65536];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        blake3_hasher_update(&hasher, buffer, static_cast<size_t>(file.gcount()));
    }

    uint8_t output[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);

    std::ostringstream oss;
    for (size_t i = 0; i < BLAKE3_OUT_LEN; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(output[i]);
    }
    std::string hash_str = oss.str();
    spdlog::debug("Pipeline::compute_hash: '{}' -> {}", file_path.filename().string(), hash_str);
    return hash_str;
}

Pipeline::Pipeline(db::Database& database, cache::CacheManager& cache_manager, std::filesystem::path working_dir)
    : db_(database), cache_mgr_(cache_manager), working_dir_(std::move(working_dir)) {}

uint64_t Pipeline::execute_scan(const std::filesystem::path& scan_root,
                                const ScanOptions& options,
                                ProgressCallback callback) {
    spdlog::debug("Pipeline::execute_scan: starting scan on '{}' (recursive={}, group_bursts={})",
                  scan_root.string(), options.recursive, options.group_bursts);

    if (!std::filesystem::exists(scan_root)) {
        spdlog::warn("Scan root directory does not exist: {}", scan_root.string());
        return 0;
    }

    // Stage 1: File discovery
    std::vector<std::filesystem::path> candidate_files;
    if (options.recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(scan_root)) {
            if (entry.is_regular_file() && is_supported_image(entry.path())) {
                candidate_files.push_back(entry.path());
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(scan_root)) {
            if (entry.is_regular_file() && is_supported_image(entry.path())) {
                candidate_files.push_back(entry.path());
            }
        }
    }

    if (candidate_files.empty()) {
        spdlog::info("No supported image files found in {}", scan_root.string());
        return 0;
    }

    uint64_t total_files = candidate_files.size();
    spdlog::debug("Pipeline: Discovered {} candidate image file(s) in '{}'", total_files, scan_root.string());

    std::atomic<uint64_t> processed_count{0};
    std::mutex results_mutex;
    std::vector<Photo> processed_photos;
    processed_photos.reserve(total_files);

    // Stage 2: Parallel image processing
    uint32_t num_threads = (options.max_threads > 0) ? options.max_threads : std::max(1u, std::thread::hardware_concurrency());
    size_t chunk_size = (total_files + num_threads - 1) / num_threads;
    spdlog::debug("Pipeline: Launching {} worker threads (chunk size: {})", num_threads, chunk_size);

    auto worker_task = [&](size_t start_idx, size_t end_idx) {
        std::vector<Photo> local_photos;
        for (size_t i = start_idx; i < end_idx; ++i) {
            const auto& file_path = candidate_files[i];

            std::string hash = compute_hash(file_path);
            if (hash.empty()) continue;

            // Check if already in DB
            {
                std::lock_guard<std::mutex> lock(results_mutex);
                if (db_.exists_by_hash(hash) || db_.exists_by_path(file_path)) {
                    spdlog::debug("Pipeline: Skipping already indexed photo: '{}'", file_path.string());
                    processed_count++;
                    if (callback) callback(processed_count.load(), total_files, file_path.filename().string());
                    continue;
                }
            }

            // Extract EXIF
            Photo p;
            metadata::ExifReader::read_from_file(file_path, p);
            p.file_path = file_path;
            p.file_size = std::filesystem::file_size(file_path);
            p.hash = hash;

            // Decode and hash
            auto img = codec::ImageCodec::decode_file(file_path);
            if (!img.empty()) {
                p.dimensions.width = img.width;
                p.dimensions.height = img.height;
                p.phash = hash::PHash::compute(img);
                p.thumbhash = hash::ThumbHash::encode_to_base64(img);

                spdlog::debug("Pipeline: Processed '{}' ({}x{}, pHash: 0x{:016x}, ThumbHash: '{}')",
                              file_path.filename().string(), p.dimensions.width, p.dimensions.height, p.phash, p.thumbhash);

                // Generate preview thumbnail if requested
                if (options.generate_previews) {
                    auto preview = codec::ImageCodec::resize_aspect_fit(img, 500, 500);
                    if (preview.empty()) preview = img;
                    cache_mgr_.put_preview(p.hash, preview);
                    spdlog::debug("Pipeline: Saved AVIF preview thumbnail for '{}'", p.hash);
                }
            } else {
                spdlog::warn("Pipeline: Could not decode image file: '{}'", file_path.string());
            }

            local_photos.push_back(std::move(p));
            processed_count++;

            if (callback) {
                std::lock_guard<std::mutex> lock(results_mutex);
                callback(processed_count.load(), total_files, file_path.filename().string());
            }
        }

        std::lock_guard<std::mutex> lock(results_mutex);
        processed_photos.insert(processed_photos.end(),
                               std::make_move_iterator(local_photos.begin()),
                               std::make_move_iterator(local_photos.end()));
    };

    std::vector<std::thread> workers;
    for (uint32_t t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(start + chunk_size, candidate_files.size());
        if (start < end) {
            workers.emplace_back(worker_task, start, end);
        }
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    if (processed_photos.empty()) {
        spdlog::debug("Pipeline: No new photos to ingest after filtering");
        return 0;
    }

    // Stage 3: Burst Detection & Ingestion
    uint64_t total_ingested = 0;
    if (options.group_bursts && processed_photos.size() >= 2) {
        spdlog::debug("Pipeline: Running burst group detection on {} photos...", processed_photos.size());
        auto burst_groups = detector::SimilarityEngine::find_burst_groups(
            processed_photos,
            options.burst_time_window_seconds,
            options.burst_max_hamming_distance
        );
        spdlog::debug("Pipeline: Identified {} burst group(s)", burst_groups.size());

        std::unordered_set<std::string> burst_photo_hashes;
        auto bursts_dir = working_dir_ / "bursts";
        if (!std::filesystem::exists(bursts_dir)) {
            std::filesystem::create_directories(bursts_dir);
        }

        for (auto& group : burst_groups) {
            if (group.photos.size() < 2) continue;

            for (const auto& bp : group.photos) {
                burst_photo_hashes.insert(bp.hash);
            }

            // Create container file name
            auto first_photo = group.photos.front();
            std::string container_name = "burst_" + first_photo.hash.substr(0, 16) + ".avif";
            auto container_path = bursts_dir / container_name;
            spdlog::debug("Pipeline: Generating burst container '{}' with {} frames",
                          container_path.string(), group.photos.size());

            // Collect frames and encode multi-frame AVIF
            std::vector<ImageBuffer> frames;
            frames.reserve(group.photos.size());
            for (const auto& bp : group.photos) {
                auto b_img = codec::ImageCodec::decode_file(bp.file_path);
                if (!b_img.empty()) {
                    frames.push_back(std::move(b_img));
                }
            }

            if (frames.size() >= 2 && codec::AvifCodec::encode_burst_sequence(frames, container_path, 80, 6)) {
                Photo container_photo = first_photo;
                container_photo.file_path = container_path;
                container_photo.file_size = std::filesystem::file_size(container_path);
                container_photo.hash = compute_hash(container_path);
                container_photo.mime_type = "image/avif";
                container_photo.is_burst_group = true;
                container_photo.frame_count = static_cast<uint32_t>(group.photos.size());

                int64_t parent_id = db_.insert_photo(container_photo);
                if (parent_id > 0) {
                    for (size_t f = 0; f < group.photos.size(); ++f) {
                        BurstFrame frame_rec;
                        frame_rec.photo_id = parent_id;
                        frame_rec.frame_index = static_cast<uint32_t>(f);
                        frame_rec.original_file_name = group.photos[f].file_path.filename().string();
                        frame_rec.width = group.photos[f].dimensions.width;
                        frame_rec.height = group.photos[f].dimensions.height;
                        frame_rec.phash = group.photos[f].phash;
                        frame_rec.thumbhash = group.photos[f].thumbhash;
                        frame_rec.exif_json = group.photos[f].exif_json;
                        db_.insert_burst_frame(frame_rec);
                    }
                    total_ingested++;
                    spdlog::debug("Pipeline: Ingested burst group container id={}", parent_id);
                }
            }
        }

        // Insert remaining non-burst photos
        std::vector<Photo> non_burst_photos;
        for (auto& p : processed_photos) {
            if (burst_photo_hashes.find(p.hash) == burst_photo_hashes.end()) {
                non_burst_photos.push_back(std::move(p));
            }
        }

        if (!non_burst_photos.empty()) {
            spdlog::debug("Pipeline: Inserting {} standalone photos into database", non_burst_photos.size());
            total_ingested += db_.insert_photos_batch(non_burst_photos);
        }
    } else {
        spdlog::debug("Pipeline: Inserting {} photos into database", processed_photos.size());
        total_ingested = db_.insert_photos_batch(processed_photos);
    }

    spdlog::info("Successfully scanned and ingested {} photos into database", total_ingested);
    return total_ingested;
}

} // namespace image_odb::core
