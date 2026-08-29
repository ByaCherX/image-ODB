#include "image_odb/database.h"
#include "image_odb/exif_reader.h"
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <iostream>

void run_db_tests() {
    using namespace image_odb;
    using namespace image_odb::db;

    const std::filesystem::path test_db = "test_photos_db_m4.db";
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }

    {
        Database db(test_db);
        db.initialize_schema();

        // Test 1: Insert Photo with Full Rich Metadata
        Photo p1;
        p1.file_path = "photos/tokyo_01.jpg";
        p1.file_size = 3500000;
        p1.hash = "1111111111111111111111111111111111111111111111111111111111111111";
        p1.dimensions.width = 6000;
        p1.dimensions.height = 4000;
        p1.dimensions.orientation = 1;
        p1.mime_type = "image/jpeg";
        p1.capture_date = metadata::ExifReader::parse_exif_date("2026:05:10 14:20:00");
        p1.location.latitude = 35.6762;
        p1.location.longitude = 139.6503;
        p1.location.altitude = 40.0;
        p1.location.place_name = "Shinjuku, Tokyo";
        p1.camera.make = "Sony";
        p1.camera.model = "ILCE-7M4";
        p1.lens.make = "Sony";
        p1.lens.model = "FE 24-70mm F2.8 GM II";
        p1.lens.focal_length_mm = 50.0;
        p1.lens.focal_length_in_35mm = 50.0;
        p1.exposure.f_number = 2.8;
        p1.exposure.exposure_time_str = "1/1000";
        p1.exposure.iso_speed = 100;
        p1.exposure.exposure_bias = 0.0;
        p1.exposure.flash_fired = false;
        p1.phash = 0x123456789ABCDEF0ULL;
        p1.thumbhash = "3OcRJYB4d3h/iIeHeEh3eIeH";
        p1.is_burst_group = false;
        p1.frame_count = 1;
        p1.exif_json = {{"software", "Lightroom"}, {"artist", "Photographer"}};

        int64_t id1 = db.insert_photo(p1);
        if (id1 <= 0) {
            throw std::runtime_error("Failed to insert photo 1");
        }

        // Test 2: Full Entity Hydration Verification
        auto fetched1 = db.find_by_id(id1);
        if (!fetched1.has_value()) {
            throw std::runtime_error("find_by_id failed to retrieve photo 1");
        }
        if (fetched1->camera.make != "Sony" || fetched1->camera.model != "ILCE-7M4" ||
            fetched1->lens.model != "FE 24-70mm F2.8 GM II" || !fetched1->exposure.f_number.has_value() ||
            *fetched1->exposure.f_number != 2.8 || fetched1->exposure.iso_speed != 100 ||
            !fetched1->location.latitude.has_value() || std::abs(*fetched1->location.latitude - 35.6762) > 0.001 ||
            fetched1->location.place_name != "Shinjuku, Tokyo" ||
            fetched1->exif_json["software"] != "Lightroom") {
            throw std::runtime_error("Hydrated photo fields do not match inserted values");
        }

        // Test 3: Find by path and Find by hash
        if (!db.exists_by_path(p1.file_path)) {
            throw std::runtime_error("exists_by_path returned false");
        }
        if (!db.exists_by_hash(p1.hash)) {
            throw std::runtime_error("exists_by_hash returned false");
        }
        auto by_hash = db.find_by_hash(p1.hash);
        if (!by_hash.has_value() || by_hash->id != id1) {
            throw std::runtime_error("find_by_hash returned incorrect photo");
        }

        // Test 4: Update photo
        p1.location.place_name = "Shibuya, Tokyo";
        p1.exposure.iso_speed = 200;
        if (!db.update_photo(p1)) {
            throw std::runtime_error("update_photo failed");
        }
        auto updated = db.find_by_id(id1);
        if (!updated.has_value() || updated->location.place_name != "Shibuya, Tokyo" || updated->exposure.iso_speed != 200) {
            throw std::runtime_error("Updated photo values were not persisted");
        }

        // Test 5: Insert Burst Container Photo + Burst Frames
        Photo burst_container;
        burst_container.file_path = "photos/burst_action.avif";
        burst_container.file_size = 1500000;
        burst_container.hash = "2222222222222222222222222222222222222222222222222222222222222222";
        burst_container.dimensions.width = 4000;
        burst_container.dimensions.height = 3000;
        burst_container.capture_date = metadata::ExifReader::parse_exif_date("2026:06:15 10:00:00");
        burst_container.camera.make = "Canon";
        burst_container.camera.model = "EOS R5";
        burst_container.lens.model = "RF 70-200mm F2.8 L";
        burst_container.is_burst_group = true;
        burst_container.frame_count = 3;

        int64_t burst_id = db.insert_photo(burst_container);
        if (burst_id <= 0) {
            throw std::runtime_error("Failed to insert burst container photo");
        }

        BurstFrame f0{ -1, burst_id, 0, "DSC_0001.JPG", 4000, 3000, 100ULL, "hash0", {{"shot", 1}} };
        BurstFrame f1{ -1, burst_id, 1, "DSC_0002.JPG", 4000, 3000, 101ULL, "hash1", {{"shot", 2}} };
        BurstFrame f2{ -1, burst_id, 2, "DSC_0003.JPG", 4000, 3000, 102ULL, "hash2", {{"shot", 3}} };

        db.insert_burst_frame(f0);
        db.insert_burst_frame(f1);
        db.insert_burst_frame(f2);

        auto frames = db.get_burst_frames(burst_id);
        if (frames.size() != 3 || frames[1].original_file_name != "DSC_0002.JPG") {
            throw std::runtime_error("get_burst_frames failed or returned incorrect frame sequence");
        }

        // Test 6: Batch insertion
        std::vector<Photo> batch;
        for (int i = 0; i < 10; ++i) {
            Photo bp;
            bp.file_path = "photos/batch_" + std::to_string(i) + ".jpg";
            bp.file_size = 100000 + i * 1000;
            bp.hash = "hash_batch_" + std::to_string(i);
            bp.dimensions.width = 1920;
            bp.dimensions.height = 1080;
            bp.camera.make = (i % 2 == 0) ? "Apple" : "Sony";
            bp.camera.model = (i % 2 == 0) ? "iPhone 15 Pro" : "ILCE-7M4";
            bp.exposure.iso_speed = 50 + i * 100;
            bp.location.place_name = "Istanbul";
            batch.push_back(bp);
        }
        uint64_t inserted_batch = db.insert_photos_batch(batch);
        if (inserted_batch != 10) {
            throw std::runtime_error("insert_photos_batch failed to insert 10 photos, got: " + std::to_string(inserted_batch));
        }

        // Test 7: Total count
        uint64_t total_count = db.count_photos();
        if (total_count != 12) { // 1 single + 1 burst + 10 batch = 12
            throw std::runtime_error("count_photos expected 12, got: " + std::to_string(total_count));
        }

        uint64_t burst_count = db.count_burst_groups();
        if (burst_count != 1) {
            throw std::runtime_error("count_burst_groups expected 1, got: " + std::to_string(burst_count));
        }

        // Test 8: Complex query filtering
        // Filter by camera make
        ListOptions opt_apple;
        opt_apple.camera_make_filter = "Apple";
        auto apple_photos = db.list_photos(opt_apple);
        if (apple_photos.size() != 5) {
            throw std::runtime_error("list_photos camera_make='Apple' expected 5 results, got: " + std::to_string(apple_photos.size()));
        }

        // Filter by ISO range
        ListOptions opt_iso;
        opt_iso.min_iso = 300;
        opt_iso.max_iso = 800;
        auto iso_photos = db.list_photos(opt_iso);
        if (iso_photos.empty()) {
            throw std::runtime_error("list_photos ISO range returned 0 results");
        }

        // Filter by burst_only
        ListOptions opt_burst;
        opt_burst.burst_only = true;
        auto burst_list = db.list_photos(opt_burst);
        if (burst_list.size() != 1 || burst_list[0].id != burst_id) {
            throw std::runtime_error("list_photos burst_only expected 1 result");
        }

        // Test 9: Cascade Deletion
        if (!db.delete_photo(burst_id)) {
            throw std::runtime_error("delete_photo failed for burst container");
        }
        auto post_delete_frames = db.get_burst_frames(burst_id);
        if (!post_delete_frames.empty()) {
            throw std::runtime_error("Foreign key ON DELETE CASCADE failed: burst_frames still exist");
        }
    }

    // Clean up
    if (std::filesystem::exists(test_db)) {
        std::filesystem::remove(test_db);
    }
}
