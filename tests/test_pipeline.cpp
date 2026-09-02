#include "image_odb/image_odb.h"
#include "image_odb/jpeg_codec.h"
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <iostream>
#include <atomic>

namespace {

image_odb::ImageBuffer make_checkerboard_pattern(uint32_t w, uint32_t h, uint32_t cell) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(static_cast<size_t>(w) * h * 3);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t val = ((x / cell) % 2 == (y / cell) % 2) ? 240 : 20;
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = val;
            buf.data[idx + 1] = val;
            buf.data[idx + 2] = val;
        }
    }
    return buf;
}

image_odb::ImageBuffer make_circle_pattern(uint32_t w, uint32_t h, int radius) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(static_cast<size_t>(w) * h * 3);

    int cx = static_cast<int>(w) / 2;
    int cy = static_cast<int>(h) / 2;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            int dx = static_cast<int>(x) - cx;
            int dy = static_cast<int>(y) - cy;
            uint8_t val = (dx * dx + dy * dy <= radius * radius) ? 250 : 10;
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = val;
            buf.data[idx + 1] = val;
            buf.data[idx + 2] = 200;
        }
    }
    return buf;
}

image_odb::ImageBuffer make_burst_pattern(uint32_t w, uint32_t h, uint8_t offset) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(static_cast<size_t>(w) * h * 3);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = static_cast<uint8_t>((y * 10 + offset) % 256);
            buf.data[idx + 1] = static_cast<uint8_t>((y * 10 + offset) % 256);
            buf.data[idx + 2] = 50;
        }
    }
    return buf;
}

} // namespace

void run_pipeline_tests() {
    using namespace image_odb;

    const std::filesystem::path test_workspace = "test_workspace_m6";
    const std::filesystem::path photos_dir = test_workspace / "input_photos";

    if (std::filesystem::exists(test_workspace)) {
        std::filesystem::remove_all(test_workspace);
    }
    std::filesystem::create_directories(photos_dir);

    // Create 2 single distinct photos (checkerboard vs circle)
    auto single1 = make_checkerboard_pattern(100, 100, 10);
    auto single2 = make_circle_pattern(100, 100, 30);
    codec::JpegCodec::encode_file(single1, photos_dir / "single_01.jpg", 90);
    codec::JpegCodec::encode_file(single2, photos_dir / "single_02.jpg", 90);

    // Create 3 burst photos (horizontal stripe patterns with minor offset)
    auto b1 = make_burst_pattern(100, 100, 0);
    auto b2 = make_burst_pattern(100, 100, 1);
    auto b3 = make_burst_pattern(100, 100, 2);
    codec::JpegCodec::encode_file(b1, photos_dir / "burst_01.jpg", 90);
    codec::JpegCodec::encode_file(b2, photos_dir / "burst_02.jpg", 90);
    codec::JpegCodec::encode_file(b3, photos_dir / "burst_03.jpg", 90);

    {
        Engine engine(test_workspace);
        if (!engine.initialize_workspace()) {
            throw std::runtime_error("Engine::initialize_workspace failed");
        }

        std::atomic<uint64_t> progress_calls{0};
        ProgressCallback callback = [&](uint64_t processed, uint64_t total, const std::string& current) {
            (void)processed;
            (void)total;
            (void)current;
            progress_calls++;
        };

        ScanOptions scan_opt;
        scan_opt.group_bursts = true;
        scan_opt.recursive = true;
        scan_opt.generate_previews = true;
        scan_opt.burst_time_window_seconds = 60; // wide enough for unit test filesystem timestamps
        scan_opt.burst_max_hamming_distance = 10;

        uint64_t ingested = engine.scan_directory(photos_dir, scan_opt, callback);
        if (ingested == 0) {
            throw std::runtime_error("Engine::scan_directory ingested 0 photos");
        }

        if (progress_calls == 0) {
            throw std::runtime_error("ProgressCallback was never invoked");
        }

        // Verify database records
        ListOptions list_opt;
        auto all_photos = engine.list_photos(list_opt);
        if (all_photos.empty()) {
            throw std::runtime_error("list_photos returned 0 records after scanning");
        }

        // Check burst container
        uint64_t burst_count = engine.database().count_burst_groups();
        if (burst_count != 1) {
            throw std::runtime_error("Expected 1 burst group container, got: " + std::to_string(burst_count));
        }

        // Verify preview retrieval
        for (const auto& p : all_photos) {
            auto preview = engine.get_preview(p.id);
            if (!preview.has_value() || preview->empty()) {
                throw std::runtime_error("Failed to retrieve preview for photo id: " + std::to_string(p.id));
            }

            if (p.is_burst_group) {
                auto frames = engine.database().get_burst_frames(p.id);
                if (frames.size() != 3) {
                    throw std::runtime_error("Expected 3 burst frames for container, got: " + std::to_string(frames.size()));
                }

                // Test extracting a frame from the burst container
                auto extracted_path = test_workspace / "extracted_frame_1.jpg";
                if (!engine.extract_frame(p.file_path, 1, extracted_path)) {
                    throw std::runtime_error("Engine::extract_frame failed for burst container");
                }
                if (!std::filesystem::exists(extracted_path) || std::filesystem::file_size(extracted_path) == 0) {
                    throw std::runtime_error("Extracted frame file is missing or empty");
                }
            }
        }

        // Second scan: Deduplication test (should ingest 0 new photos)
        uint64_t second_ingest = engine.scan_directory(photos_dir, scan_opt, nullptr);
        if (second_ingest != 0) {
            throw std::runtime_error("Second scan failed deduplication, ingested: " + std::to_string(second_ingest));
        }
    }

    // Clean up
    if (std::filesystem::exists(test_workspace)) {
        std::filesystem::remove_all(test_workspace);
    }
}
