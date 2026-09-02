#include "image_odb/image_odb.h"
#include "image_odb/jpeg_codec.h"
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <iostream>

namespace {

image_odb::ImageBuffer make_test_image(uint32_t w, uint32_t h) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(static_cast<size_t>(w) * h * 3);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = static_cast<uint8_t>((x * 255) / w);
            buf.data[idx + 1] = static_cast<uint8_t>((y * 255) / h);
            buf.data[idx + 2] = 128;
        }
    }
    return buf;
}

} // namespace

void run_convert_and_cache_tests() {
    using namespace image_odb;

    const std::filesystem::path test_dir = "test_convert_workspace";
    if (std::filesystem::exists(test_dir)) {
        std::error_code ec;
        std::filesystem::remove_all(test_dir, ec);
    }
    std::filesystem::create_directories(test_dir);

    // 1. Test Format Detection and Centralized Extension Helpers
    assert(codec::ImageCodec::detect_format("photo.jpg") == ImageFormat::JPEG);
    assert(codec::ImageCodec::detect_format("photo.jpeg") == ImageFormat::JPEG);
    assert(codec::ImageCodec::detect_format("photo.avif") == ImageFormat::AVIF);
    assert(codec::ImageCodec::detect_format("photo.png") == ImageFormat::PNG);
    assert(codec::ImageCodec::detect_format("photo.webp") == ImageFormat::WEBP);
    assert(is_decoding_supported_image("photo.jpg"));
    assert(is_decoding_supported_image("photo.avif"));
    assert(!is_decoding_supported_image("photo.png"));
    assert(is_decoding_supported_extension(".jpg"));
    assert(is_decoding_supported_extension(".AVIF"));
    assert(!is_decoding_supported_extension(".webp"));
    assert(is_supported_image("photo.jpg"));
    assert(is_supported_image("photo.AVIF"));
    assert(is_supported_image("photo.webp"));
    assert(is_supported_image("photo.jfif"));
    assert(is_supported_image("photo.bmp"));
    assert(!is_supported_image("document.txt"));
    assert(!is_supported_image("document.pdf"));
    assert(is_supported_extension(".JPG"));
    assert(is_supported_extension(".avif"));
    assert(!is_supported_extension(".exe"));
    assert(is_decoding_supported(ImageFormat::JPEG));
    assert(is_decoding_supported(ImageFormat::AVIF));
    assert(!is_decoding_supported(ImageFormat::PNG));
    assert(is_encoding_supported(ImageFormat::AVIF));
    assert(is_encoding_supported(ImageFormat::JPEG));
    assert(!is_encoding_supported(ImageFormat::WEBP));
    assert(SUPPORTED_IMAGE_EXTENSIONS.size() == 10);

    // 2. Test Filename Date Fallback Parsing
    auto dt1 = metadata::ExifReader::parse_date_from_filename("IMG_20240815_134520.jpg");
    assert(dt1.has_value());
    std::string s1 = metadata::ExifReader::format_iso8601(*dt1);
    assert(s1.find("2024-08-15") != std::string::npos);

    auto dt2 = metadata::ExifReader::parse_date_from_filename("2023-11-20_09-15-30.jpg");
    assert(dt2.has_value());
    std::string s2 = metadata::ExifReader::format_iso8601(*dt2);
    assert(s2.find("2023-11-20") != std::string::npos);

    auto dt3 = metadata::ExifReader::parse_date_from_filename("Screenshot_20250610-182045.png");
    assert(dt3.has_value());
    std::string s3 = metadata::ExifReader::format_iso8601(*dt3);
    assert(s3.find("2025-06-10") != std::string::npos);

    auto dt4 = metadata::ExifReader::parse_date_from_filename("20220412.jpg");
    assert(dt4.has_value());
    std::string s4 = metadata::ExifReader::format_iso8601(*dt4);
    assert(s4.find("2022-04-12") != std::string::npos);

    // 3. Test CacheMode
    {
        cache::CacheManager cm(test_dir / "cache_test", 1024 * 1024);
        cm.set_cache_mode(CacheMode::NONE);
        assert(cm.cache_mode() == CacheMode::NONE);

        auto img = make_test_image(80, 80);
        cm.put_preview("key1", img);
        assert(!cm.memory_cache().contains("key1"));
        assert(!cm.disk_cache().has_preview("key1"));

        cm.set_cache_mode(CacheMode::DISK_ONLY);
        cm.put_preview("key2", img);
        assert(!cm.memory_cache().contains("key2"));
        assert(cm.disk_cache().has_preview("key2"));

        cm.set_cache_mode(CacheMode::RAM_ONLY);
        cm.put_preview("key3", img);
        assert(cm.memory_cache().contains("key3"));
        assert(!cm.disk_cache().has_preview("key3"));
    }

    // 4. Test Single File Convert (JPEG -> AVIF with embed_thumbnail, and AVIF -> JPEG)
    auto src_img = make_test_image(120, 120);
    auto raw_jpg_path = test_dir / "IMG_20240815_134520.jpg";
    codec::JpegCodec::encode_file(src_img, raw_jpg_path, 90);

    {
        Engine engine(test_dir);
        engine.initialize_workspace();

        auto out_avif_path = test_dir / "converted_image.avif";
        EncodeOptions enc_avif;
        enc_avif.format = ImageFormat::AVIF;
        enc_avif.quality = 85;
        enc_avif.speed = 8;
        enc_avif.embed_thumbnail = true;
        enc_avif.thumbnail_dimension = 64;

        bool conv_ok = engine.convert_file(raw_jpg_path, out_avif_path, enc_avif);
        assert(conv_ok);
        assert(std::filesystem::exists(out_avif_path) && std::filesystem::file_size(out_avif_path) > 0);

        // Convert AVIF back to JPEG
        auto back_jpg_path = test_dir / "converted_back.jpg";
        EncodeOptions enc_jpg;
        enc_jpg.format = ImageFormat::JPEG;
        enc_jpg.quality = 90;
        bool back_ok = engine.convert_file(out_avif_path, back_jpg_path, enc_jpg);
        assert(back_ok);
        assert(std::filesystem::exists(back_jpg_path) && std::filesystem::file_size(back_jpg_path) > 0);

        // 5. Test Scan with --convert (convert_to_avif = true)
        auto scan_input_dir = test_dir / "raw_photos";
        std::filesystem::create_directories(scan_input_dir);
        auto photo1_jpg = scan_input_dir / "IMG_20240815_134520.jpg";
        auto photo2_jpg = scan_input_dir / "IMG_20240816_140000.jpg";
        codec::JpegCodec::encode_file(src_img, photo1_jpg, 90);
        codec::JpegCodec::encode_file(src_img, photo2_jpg, 90);

        ScanOptions scan_opts;
        scan_opts.convert_to_avif = true;
        scan_opts.convert_options.quality = 80;
        scan_opts.convert_options.speed = 9;
        scan_opts.convert_options.embed_thumbnail = true;
        scan_opts.delete_source = true;
        scan_opts.cache_mode = CacheMode::ALL;

        uint64_t ingested = engine.scan_directory(scan_input_dir, scan_opts, nullptr);
        assert(ingested == 2);

        // Verify source was replaced by converted .avif files
        assert(!std::filesystem::exists(photo1_jpg));
        assert(std::filesystem::exists(scan_input_dir / "IMG_20240815_134520.avif"));
        assert(std::filesystem::exists(scan_input_dir / "IMG_20240816_140000.avif"));

        // Verify database entries
        ListOptions list_opts;
        auto photos = engine.list_photos(list_opts);
        assert(photos.size() == 2);
        for (const auto& p : photos) {
            assert(p.mime_type == "image/avif");
            assert(p.file_path.extension() == ".avif");
            assert(p.capture_date.has_value()); // Date parsed from filename fallback!
        }

        // 6. Test convert_photo_by_id
        if (!photos.empty()) {
            auto export_jpg = test_dir / "exported_photo.jpg";
            EncodeOptions exp_opt;
            exp_opt.format = ImageFormat::JPEG;
            exp_opt.quality = 85;
            assert(engine.convert_photo_by_id(photos[0].id, export_jpg, exp_opt));
            assert(std::filesystem::exists(export_jpg));
        }
    }

    // Clean up after engine is closed
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
}
