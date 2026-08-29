#include "image_odb/thumbhash.h"
#include <cassert>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <cmath>

namespace {

image_odb::ImageBuffer make_test_photo(uint32_t w, uint32_t h) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(w * h * 3);

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = static_cast<uint8_t>((x * 255) / w);       // Red gradient
            buf.data[idx + 1] = static_cast<uint8_t>((y * 255) / h);   // Green gradient
            buf.data[idx + 2] = 128;                                   // Constant Blue
        }
    }
    return buf;
}

} // namespace

void run_thumbhash_tests() {
    using namespace image_odb;
    using namespace image_odb::hash;

    // Test 1: Basic ThumbHash encoding
    auto img = make_test_photo(100, 75); // 4:3 landscape
    std::string b64 = ThumbHash::encode_to_base64(img);
    if (b64.empty()) {
        throw std::runtime_error("ThumbHash Base64 encoding produced empty string");
    }

    if (b64.size() < 20 || b64.size() > 50) {
        throw std::runtime_error("ThumbHash Base64 unexpected length: " + std::to_string(b64.size()));
    }

    // Test 2: Binary encoding payload size
    auto binary = ThumbHash::encode(img);
    if (binary.size() < 15 || binary.size() > 35) {
        throw std::runtime_error("ThumbHash binary size unexpected: " + std::to_string(binary.size()));
    }

    // Test 3: Metadata extraction
    auto info = ThumbHash::extract_info(binary);
    if (info.has_alpha) {
        throw std::runtime_error("RGB image should not have alpha flag set");
    }
    if (info.approximate_aspect_ratio <= 0.0f) {
        throw std::runtime_error("Invalid approximate aspect ratio extracted");
    }

    // Test 4: Inverse DCT Decoding to RGBA
    auto preview = ThumbHash::decode_from_base64(b64, 32, 24);
    if (preview.empty() || preview.width != 32 || preview.height != 24 || preview.channels != 4) {
        throw std::runtime_error("ThumbHash decoding failed to produce valid 32x24 RGBA preview");
    }

    // Verify reconstructed pixels are within valid range and non-trivial
    bool has_nonzero = false;
    for (size_t i = 0; i < preview.data.size(); ++i) {
        if (preview.data[i] > 0) has_nonzero = true;
    }
    if (!has_nonzero) {
        throw std::runtime_error("ThumbHash preview is all zeros");
    }

    // Test 5: Alpha channel encoding
    ImageBuffer alpha_img;
    alpha_img.width = 50;
    alpha_img.height = 50;
    alpha_img.channels = 4;
    alpha_img.format = PixelFormat::RGBA8;
    alpha_img.data.assign(50 * 50 * 4, 128);
    alpha_img.data[3] = 64; // Set a transparent pixel

    auto alpha_bytes = ThumbHash::encode(alpha_img);
    auto alpha_info = ThumbHash::extract_info(alpha_bytes);
    if (!alpha_info.has_alpha) {
        throw std::runtime_error("Alpha channel was not detected in ThumbHash encoding");
    }

    // Test 6: Empty input
    ImageBuffer empty_img;
    if (!ThumbHash::encode(empty_img).empty()) {
        throw std::runtime_error("Empty image should produce empty ThumbHash");
    }
}
