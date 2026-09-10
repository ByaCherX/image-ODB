#include "image_odb/image_odb.h"
#include "image_odb/jpeg_codec.h"
#include <cassert>
#include <stdexcept>
#include <filesystem>
#include <fstream>
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

void run_convert_tests() {
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
    assert(codec::ImageCodec::detect_format("photo.jfif") == ImageFormat::JPEG);
    assert(codec::ImageCodec::detect_format("photo.avif") == ImageFormat::AVIF);
    assert(codec::ImageCodec::detect_format("photo.avifs") == ImageFormat::AVIF);
    assert(codec::ImageCodec::detect_format("photo.png") == ImageFormat::PNG);
    assert(codec::ImageCodec::detect_format("photo.webp") == ImageFormat::WEBP);
    assert(codec::ImageCodec::detect_format("photo.bmp") == ImageFormat::BMP);
    assert(codec::ImageCodec::detect_format("photo.tif") == ImageFormat::TIFF);
    assert(codec::ImageCodec::detect_format("photo.tiff") == ImageFormat::TIFF);
    assert(codec::ImageCodec::detect_format("document.txt") == ImageFormat::UNKNOWN);
    assert(codec::ImageCodec::detect_format("document.pdf") == ImageFormat::UNKNOWN);
    assert(codec::ImageCodec::detect_format("program.exe") == ImageFormat::UNKNOWN);
    assert(codec::SUPPORTED_IMAGE_EXTENSIONS.size() == 10);
    assert(codec::SUPPORTED_DECODE_EXTENSIONS.size() == 6);
    assert(std::find(codec::SUPPORTED_DECODE_EXTENSIONS.begin(),
                     codec::SUPPORTED_DECODE_EXTENSIONS.end(),
                     ".png") != codec::SUPPORTED_DECODE_EXTENSIONS.end());

    // 2. Test Filename Date Fallback Parsing
    auto dt1 = metadata::ExifReader::parse_date_from_filename("IMG_20240815_134520.jpg");
    assert(dt1.has_value());
    std::string s1 = image_odb::util::format_iso8601(*dt1);
    assert(s1.find("2024-08-15") != std::string::npos);

    auto dt2 = metadata::ExifReader::parse_date_from_filename("2023-11-20_09-15-30.jpg");
    assert(dt2.has_value());
    std::string s2 = image_odb::util::format_iso8601(*dt2);
    assert(s2.find("2023-11-20") != std::string::npos);

    auto dt3 = metadata::ExifReader::parse_date_from_filename("Screenshot_20250610-182045.png");
    assert(dt3.has_value());
    std::string s3 = image_odb::util::format_iso8601(*dt3);
    assert(s3.find("2025-06-10") != std::string::npos);

    auto dt4 = metadata::ExifReader::parse_date_from_filename("20220412.jpg");
    assert(dt4.has_value());
    std::string s4 = image_odb::util::format_iso8601(*dt4);
    assert(s4.find("2022-04-12") != std::string::npos);

    // 3. Test Single File Convert (JPEG -> AVIF with embed_thumbnail, and AVIF -> JPEG)
    auto src_img = make_test_image(120, 120);
    auto raw_jpg_path = test_dir / "IMG_20240815_134520.jpg";
    EncodeOptions jpg_opts{.format = ImageFormat::JPEG, .quality = 90};
    codec::ImageCodec::encode_file(src_img, raw_jpg_path, jpg_opts);

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

        [[maybe_unused]] bool conv_ok = engine.convert_file(raw_jpg_path, out_avif_path, enc_avif);
        assert(conv_ok);
        assert(std::filesystem::exists(out_avif_path) && std::filesystem::file_size(out_avif_path) > 0);

        // Convert AVIF back to JPEG
        auto back_jpg_path = test_dir / "converted_back.jpg";
        EncodeOptions enc_jpg;
        enc_jpg.format = ImageFormat::JPEG;
        enc_jpg.quality = 90;
        [[maybe_unused]] bool back_ok = engine.convert_file(out_avif_path, back_jpg_path, enc_jpg);
        assert(back_ok);
        assert(std::filesystem::exists(back_jpg_path) && std::filesystem::file_size(back_jpg_path) > 0);

        // 4. Test Scan with --convert (convert_to_avif = true)
        auto scan_input_dir = test_dir / "raw_photos";
        std::filesystem::create_directories(scan_input_dir);
        auto photo1_jpg = scan_input_dir / "IMG_20240815_134520.jpg";
        auto photo2_jpg = scan_input_dir / "IMG_20240816_140000.jpg";
        codec::ImageCodec::encode_file(src_img, photo1_jpg, jpg_opts);
        codec::ImageCodec::encode_file(src_img, photo2_jpg, jpg_opts);

        ScanOptions scan_opts;
        scan_opts.convert_to_avif = true;
        scan_opts.convert_options.quality = 80;
        scan_opts.convert_options.speed = 9;
        scan_opts.convert_options.embed_thumbnail = true;
        scan_opts.delete_source = true;

        [[maybe_unused]] uint64_t ingested = engine.scan_directory(scan_input_dir, scan_opts, nullptr);
        assert(ingested == 2);

        // Verify source was replaced by converted .avif files
        assert(!std::filesystem::exists(photo1_jpg));
        assert(std::filesystem::exists(scan_input_dir / "IMG_20240815_134520.avif"));
        assert(std::filesystem::exists(scan_input_dir / "IMG_20240816_140000.avif"));

        // Verify database entries
        ListOptions list_opts;
        auto photos = engine.list_photos(list_opts);
        assert(photos.size() == 2);
        for ([[maybe_unused]] const auto& p : photos) {
            assert(p.mime_type == "image/avif");
            assert(p.file_path.extension() == ".avif");
            assert(p.capture_date.has_value()); // Date parsed from filename fallback!
        }

        // 5. Test convert_photo_by_id
        if (!photos.empty()) {
            auto export_jpg = test_dir / "exported_photo.jpg";
            EncodeOptions exp_opt;
            exp_opt.format = ImageFormat::JPEG;
            exp_opt.quality = 85;
            assert(engine.convert_photo_by_id(photos[0].id, export_jpg, exp_opt));
            assert(std::filesystem::exists(export_jpg));
        }

        // 6. Test PNG Decode Support via libspng
        // Minimal valid 1x1 Red RGB PNG
        const std::vector<uint8_t> tiny_png = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, // PNG Signature
            0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, // IHDR
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // 1x1
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, // 8-bit RGB, CRC
            0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, // IDAT
            0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, // compressed data
            0x18, 0xdd, 0x8d, 0xb0, // CRC
            0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 // IEND
        };

        assert(codec::ImageCodec::detect_format(tiny_png) == ImageFormat::PNG);

        auto decoded_png = codec::ImageCodec::decode_memory(tiny_png);
        assert(!decoded_png.empty());
        assert(decoded_png.width == 1);
        assert(decoded_png.height == 1);
        assert(decoded_png.channels == 3);
        assert(decoded_png.format == PixelFormat::RGB8);
        assert(decoded_png.data.size() == 3);
        assert(decoded_png.data[0] == 255); // Red

        // Test PNG decode with GRAY8 target format
        DecodeOptions gray_opt{.target_format = PixelFormat::GRAY8};
        auto decoded_gray = codec::ImageCodec::decode_memory(tiny_png, "", gray_opt);
        assert(!decoded_gray.empty());
        assert(decoded_gray.width == 1);
        assert(decoded_gray.height == 1);
        assert(decoded_gray.channels == 1);
        assert(decoded_gray.format == PixelFormat::GRAY8);
        assert(decoded_gray.data.size() == 1);

        // Test PNG decode from file and conversion to AVIF
        auto sample_png_path = test_dir / "tiny.png";
        {
            std::ofstream png_out(sample_png_path, std::ios::binary);
            png_out.write(reinterpret_cast<const char*>(tiny_png.data()), static_cast<std::streamsize>(tiny_png.size()));
        }
        auto file_decoded = codec::ImageCodec::decode_file(sample_png_path);
        assert(!file_decoded.empty());
        assert(file_decoded.width == 1);
        assert(file_decoded.height == 1);

        auto png_to_avif_path = test_dir / "from_png.avif";
        EncodeOptions enc_png_to_avif;
        enc_png_to_avif.format = ImageFormat::AVIF;
        enc_png_to_avif.quality = 80;
        assert(engine.convert_file(sample_png_path, png_to_avif_path, enc_png_to_avif));
        assert(std::filesystem::exists(png_to_avif_path) && std::filesystem::file_size(png_to_avif_path) > 0);
    }

    // Clean up after engine is closed
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);
}
