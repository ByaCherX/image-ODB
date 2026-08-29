#include "image_odb/lru_cache.h"
#include "image_odb/disk_cache.h"
#include "image_odb/cache_manager.h"
#include "image_odb/jpeg_codec.h"
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <iostream>

namespace {

image_odb::ImageBuffer make_dummy_image(uint32_t w, uint32_t h, uint8_t fill) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.assign(static_cast<size_t>(w) * h * 3, fill);
    return buf;
}

} // namespace

void run_cache_tests() {
    using namespace image_odb;
    using namespace image_odb::cache;

    const std::filesystem::path temp_cache_dir = "test_cache_temp";
    if (std::filesystem::exists(temp_cache_dir)) {
        std::filesystem::remove_all(temp_cache_dir);
    }

    // =========================================================================
    // Part 1: In-Memory LRU Cache Tests
    // =========================================================================
    {
        // Capacity: 2000 bytes
        LruMemoryCache lru(2000);

        auto buf1 = make_dummy_image(10, 10, 50);  // 300 bytes
        auto buf2 = make_dummy_image(20, 20, 100); // 1200 bytes
        auto buf3 = make_dummy_image(20, 10, 150); // 600 bytes

        lru.put("img1", buf1);
        lru.put("img2", buf2); // Total in RAM: 1500 bytes <= 2000

        if (!lru.contains("img1") || !lru.contains("img2")) {
            throw std::runtime_error("LRU cache should contain img1 and img2");
        }

        // Access img1 so it becomes more recently used than img2
        auto accessed1 = lru.get("img1");
        if (!accessed1.has_value() || lru.hit_count() != 1) {
            throw std::runtime_error("LRU hit tracking failed");
        }

        // Adding img3 (600 bytes) -> total would be 300 + 1200 + 600 = 2100 > 2000.
        // img2 is the least recently used, so img2 must be evicted!
        lru.put("img3", buf3);

        if (!lru.contains("img1") || !lru.contains("img3")) {
            throw std::runtime_error("LRU cache must keep img1 and img3");
        }
        if (lru.contains("img2")) {
            throw std::runtime_error("LRU cache should have evicted img2");
        }

        // Test remove
        if (!lru.remove("img1") || lru.contains("img1")) {
            throw std::runtime_error("LRU remove failed");
        }

        // Test clear
        lru.clear();
        if (lru.current_size_bytes() != 0 || lru.item_count() != 0) {
            throw std::runtime_error("LRU clear failed");
        }
    }

    // =========================================================================
    // Part 2: Disk Cache Tests
    // =========================================================================
    {
        DiskCache disk(temp_cache_dir / ".disk_cache");
        disk.initialize();

        auto img = make_dummy_image(100, 100, 200);

        // Save preview as AVIF and JPEG
        auto avif_saved = disk.save_preview("photo_101", img, PreviewFormat::AVIF);
        auto jpg_saved = disk.save_preview("photo_102", img, PreviewFormat::JPEG);

        if (avif_saved.empty() || jpg_saved.empty()) {
            throw std::runtime_error("DiskCache::save_preview failed");
        }

        if (!disk.has_preview("photo_101", PreviewFormat::AVIF)) {
            throw std::runtime_error("DiskCache::has_preview returned false for AVIF");
        }
        if (!disk.has_preview("photo_102", PreviewFormat::JPEG)) {
            throw std::runtime_error("DiskCache::has_preview returned false for JPEG");
        }

        // Load preview
        auto loaded_avif = disk.load_preview("photo_101", PreviewFormat::AVIF);
        if (!loaded_avif.has_value() || loaded_avif->width != 100 || loaded_avif->height != 100) {
            throw std::runtime_error("DiskCache::load_preview failed for AVIF");
        }

        auto loaded_jpg = disk.load_preview("photo_102", PreviewFormat::JPEG);
        if (!loaded_jpg.has_value() || loaded_jpg->width != 100 || loaded_jpg->height != 100) {
            throw std::runtime_error("DiskCache::load_preview failed for JPEG");
        }

        if (disk.preview_count() != 2 || disk.total_size_bytes() == 0) {
            throw std::runtime_error("DiskCache count or size calculation error");
        }

        // Delete single preview
        if (!disk.delete_preview("photo_101")) {
            throw std::runtime_error("DiskCache::delete_preview failed");
        }
        if (disk.has_preview("photo_101", PreviewFormat::AVIF)) {
            throw std::runtime_error("Deleted preview still exists on disk");
        }

        // Clear disk cache
        disk.clear();
        if (disk.preview_count() != 0 || disk.total_size_bytes() != 0) {
            throw std::runtime_error("DiskCache::clear failed to remove all files");
        }
    }

    // =========================================================================
    // Part 3: Coordinated Two-Tier CacheManager Tests
    // =========================================================================
    {
        const auto source_img_path = temp_cache_dir / "original.jpg";
        auto orig = make_dummy_image(800, 600, 120);
        codec::JpegCodec::encode_file(orig, source_img_path, 90);

        CacheManager manager(temp_cache_dir / ".two_tier_cache", 10 * 1024 * 1024);

        // Cascade 1: Synthesis from source file
        auto preview1 = manager.get_or_create_preview("orig_1", source_img_path, PreviewFormat::AVIF, 200, 200);
        if (!preview1.has_value() || preview1->width > 200 || preview1->height > 200) {
            throw std::runtime_error("CacheManager failed to synthesize and downscale preview");
        }

        // Cascade 2: RAM Cache hit
        if (manager.memory_cache().hit_count() != 0) {
            throw std::runtime_error("Expected 0 initial RAM hits before second access");
        }
        auto preview2 = manager.get_or_create_preview("orig_1", source_img_path, PreviewFormat::AVIF, 200, 200);
        if (!preview2.has_value() || manager.memory_cache().hit_count() != 1) {
            throw std::runtime_error("CacheManager failed to serve from RAM cache on second lookup");
        }

        // Cascade 3: Clear RAM cache and verify Disk Cache hit
        manager.purge(true); // memory only
        if (manager.memory_cache().item_count() != 0) {
            throw std::runtime_error("RAM cache should be empty after memory-only purge");
        }

        auto preview3 = manager.get_or_create_preview("orig_1", source_img_path, PreviewFormat::AVIF, 200, 200);
        if (!preview3.has_value() || !manager.memory_cache().contains("orig_1_avif")) {
            throw std::runtime_error("CacheManager failed to reload preview from disk and repopulate RAM cache");
        }

        // Cascade 4: Full purge
        manager.purge(false);
        if (manager.disk_cache().preview_count() != 0 || manager.memory_cache().item_count() != 0) {
            throw std::runtime_error("Full purge failed");
        }
    }

    // Clean up temporary test directories
    if (std::filesystem::exists(temp_cache_dir)) {
        std::filesystem::remove_all(temp_cache_dir);
    }
}
