#include "image_odb/jpeg_codec.h"
#include "image_odb/avif_codec.h"
#include "image_odb/image_codec.h"
#include <cassert>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <filesystem>

namespace {

image_odb::ImageBuffer make_test_pattern(uint32_t w, uint32_t h, uint8_t offset) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(static_cast<size_t>(w) * h * 3);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = static_cast<uint8_t>((x * 255 / w + offset) % 256);
            buf.data[idx + 1] = static_cast<uint8_t>((y * 255 / h + offset) % 256);
            buf.data[idx + 2] = static_cast<uint8_t>((128 + offset) % 256);
        }
    }
    return buf;
}

} // namespace

void run_avif_codec_tests() {
    using namespace image_odb;
    using namespace image_odb::codec;

    const std::filesystem::path temp_dir = "test_codec_temp";
    if (!std::filesystem::exists(temp_dir)) {
        std::filesystem::create_directories(temp_dir);
    }

    const auto jpg_path = temp_dir / "sample.jpg";
    const auto avif_still_path = temp_dir / "sample_still.avif";
    const auto avif_burst_path = temp_dir / "sample_burst.avif";

    // Clean any prior artifacts
    if (std::filesystem::exists(jpg_path)) std::filesystem::remove(jpg_path);
    if (std::filesystem::exists(avif_still_path)) std::filesystem::remove(avif_still_path);
    if (std::filesystem::exists(avif_burst_path)) std::filesystem::remove(avif_burst_path);

    // Test 1: JPEG encode to file and decode back
    auto original = make_test_pattern(128, 96, 0);
    EncodeOptions jpg_file_opts;
    jpg_file_opts.format = ImageFormat::JPEG;
    jpg_file_opts.quality = 90;
    if (!ImageCodec::encode_file(original, jpg_path, jpg_file_opts)) {
        throw std::runtime_error("ImageCodec::encode_file (JPEG) failed");
    }

    auto decoded_jpg = ImageCodec::decode_file(jpg_path);
    if (decoded_jpg.empty() || decoded_jpg.width != 128 || decoded_jpg.height != 96) {
        throw std::runtime_error("ImageCodec::decode_file (JPEG) failed to restore dimensions");
    }

    // Test 2: JPEG in-memory encode and decode
    EncodeOptions mem_opts;
    mem_opts.quality = 85;
    auto mem_jpg = JpegCodec::encode_memory(original, mem_opts);
    if (mem_jpg.empty()) {
        throw std::runtime_error("JpegCodec::encode_memory failed");
    }

    auto decoded_mem = JpegCodec::decode_memory(mem_jpg);
    if (decoded_mem.empty() || decoded_mem.width != 128 || decoded_mem.height != 96) {
        throw std::runtime_error("JpegCodec::decode_memory failed");
    }

    // Test 2b: JPEG encode and decode preserving EXIF marker via jpeg_save_markers
    ImageBuffer original_with_exif = original;
    original_with_exif.exif_data = {
        'E', 'x', 'i', 'f', 0x00, 0x00,
        0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    auto mem_jpg_exif = JpegCodec::encode_memory(original_with_exif, mem_opts);
    if (mem_jpg_exif.empty()) {
        throw std::runtime_error("JpegCodec::encode_memory with EXIF failed");
    }
    auto decoded_mem_exif = JpegCodec::decode_memory(mem_jpg_exif);
    if (decoded_mem_exif.empty() || decoded_mem_exif.exif_data.empty()) {
        throw std::runtime_error("JpegCodec::decode_memory failed to extract EXIF via jpeg_save_markers");
    }
    if (decoded_mem_exif.exif_data != original_with_exif.exif_data) {
        throw std::runtime_error("EXIF payload mismatch in JpegCodec decode_memory");
    }

    // Test 3: Corrupted JPEG exception-safe recovery
    std::vector<uint8_t> corrupted_data = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x99, 0x88 };
    auto corrupt_res = JpegCodec::decode_memory(corrupted_data);
    if (!corrupt_res.empty()) {
        throw std::runtime_error("Corrupted JPEG should return empty ImageBuffer");
    }

    // Test 4: AVIF Still Image encode and decode
    EncodeOptions avif_opts;
    avif_opts.format = ImageFormat::AVIF;
    avif_opts.quality = 80;
    avif_opts.speed = 6;
    if (!ImageCodec::encode_file(original, avif_still_path, avif_opts)) {
        throw std::runtime_error("ImageCodec::encode_file (AVIF) failed");
    }

    auto decoded_avif = ImageCodec::decode_file(avif_still_path);
    if (decoded_avif.empty() || decoded_avif.width != 128 || decoded_avif.height != 96) {
        throw std::runtime_error("Avif still decode failed");
    }

    // Test 5: AVIF Multi-Frame Sequence / Burst Encoding (5 similar frames)
    std::vector<ImageBuffer> burst_frames;
    for (uint8_t i = 0; i < 5; ++i) {
        burst_frames.push_back(make_test_pattern(128, 96, i * 2)); // Slightly shifting burst motion
    }

    EncodeOptions burst_opts;
    burst_opts.quality = 80;
    burst_opts.speed = 6;
    if (!ImageCodec::encode_burst_file(burst_frames, avif_burst_path, burst_opts)) {
        throw std::runtime_error("ImageCodec::encode_burst_file failed");
    }

    // Test 6: Verify frame count
    uint32_t frame_count = ImageCodec::get_frame_count(avif_burst_path);
    if (frame_count != 5) {
        throw std::runtime_error("Expected 5 frames in AVIF burst container, got: " + std::to_string(frame_count));
    }

    // Test 7: Extract individual frames (Frame 0 Keyframe, Frame 2, Frame 4 P-Frames)
    for (uint32_t idx : {0u, 2u, 4u}) {
        auto extracted = ImageCodec::extract_frame(avif_burst_path, idx);
        if (extracted.empty() || extracted.width != 128 || extracted.height != 96) {
            throw std::runtime_error("Failed to extract frame " + std::to_string(idx) + " from burst container");
        }
    }

    // Test 8: EncodeOptions with ChromaSubsampling (YUV444) and 10-bit HDR ColorProfile
    const auto avif_hdr_path = temp_dir / "sample_hdr.avif";
    EncodeOptions hdr_opts;
    hdr_opts.format = ImageFormat::AVIF;
    hdr_opts.quality = 90;
    hdr_opts.speed = 6;
    hdr_opts.subsampling = ChromaSubsampling::YUV444;
    hdr_opts.bit_depth = 10;

    ImageBuffer hdr_buf = original;
    hdr_buf.color_profile.primaries = ColorPrimaries::BT2020;
    hdr_buf.color_profile.transfer = TransferCharacteristics::PQ;

    if (!ImageCodec::encode_file(hdr_buf, avif_hdr_path, hdr_opts)) {
        throw std::runtime_error("ImageCodec with 10-bit YUV444 HDR EncodeOptions failed");
    }

    auto decoded_hdr = ImageCodec::decode_file(avif_hdr_path);
    if (decoded_hdr.empty() || decoded_hdr.width != 128 || decoded_hdr.height != 96) {
        throw std::runtime_error("Failed to decode HDR AVIF");
    }
    if (decoded_hdr.color_profile.primaries != ColorPrimaries::BT2020 ||
        decoded_hdr.color_profile.transfer != TransferCharacteristics::PQ) {
        throw std::runtime_error("HDR ColorProfile metadata was not preserved in AVIF decode");
    }

    // Test 9: JPEG DecodeOptions downscale factor test
    DecodeOptions dec_opts;
    dec_opts.downscale_factor = 2;
    auto half_decoded_jpg = ImageCodec::decode_file(jpg_path, dec_opts);
    if (half_decoded_jpg.empty() || half_decoded_jpg.width != 64 || half_decoded_jpg.height != 48) {
        throw std::runtime_error("ImageCodec 1/2 downscaling decode failed");
    }

    // Test 10: EXIF preservation between ImageBuffer and AVIF
    const auto avif_exif_path = temp_dir / "sample_exif.avif";
    ImageBuffer exif_buf = original;
    // Minimal valid TIFF EXIF payload starting with Exif\0\0 + II*\0 (Intel little-endian)
    std::vector<uint8_t> sample_exif = {
        'E', 'x', 'i', 'f', 0x00, 0x00,
        0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, // TIFF header (offset to IFD0 = 8)
        0x00, 0x00 // 0 tags in IFD0
    };
    exif_buf.exif_data = sample_exif;

    EncodeOptions exif_enc_opts;
    exif_enc_opts.format = ImageFormat::AVIF;
    if (!ImageCodec::encode_file(exif_buf, avif_exif_path, exif_enc_opts)) {
        throw std::runtime_error("Failed to encode AVIF with EXIF metadata");
    }

    auto decoded_exif_avif = ImageCodec::decode_file(avif_exif_path);
    if (decoded_exif_avif.empty()) {
        throw std::runtime_error("Failed to decode AVIF with EXIF metadata");
    }
    if (decoded_exif_avif.exif_data.empty()) {
        throw std::runtime_error("EXIF metadata was lost during AVIF encode/decode");
    }

    // Clean up test files
    if (std::filesystem::exists(temp_dir)) {
        std::filesystem::remove_all(temp_dir);
    }
}
