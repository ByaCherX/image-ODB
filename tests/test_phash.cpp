#include "image_odb/phash.h"
#include <cassert>
#include <stdexcept>
#include <iostream>
#include <vector>

namespace {

image_odb::ImageBuffer make_solid_color_image(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(w * h * 3);
    for (size_t i = 0; i < w * h; ++i) {
        buf.data[i * 3] = r;
        buf.data[i * 3 + 1] = g;
        buf.data[i * 3 + 2] = b;
    }
    return buf;
}

image_odb::ImageBuffer make_checkerboard_image(uint32_t w, uint32_t h, uint32_t block_size) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(w * h * 3);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            bool white = ((x / block_size) + (y / block_size)) % 2 == 0;
            uint8_t val = white ? 255 : 0;
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = val;
            buf.data[idx + 1] = val;
            buf.data[idx + 2] = val;
        }
    }
    return buf;
}

image_odb::ImageBuffer make_gradient_image(uint32_t w, uint32_t h) {
    image_odb::ImageBuffer buf;
    buf.width = w;
    buf.height = h;
    buf.channels = 3;
    buf.format = image_odb::PixelFormat::RGB8;
    buf.data.resize(w * h * 3);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t val = static_cast<uint8_t>((x * 255) / (w - 1));
            size_t idx = (y * w + x) * 3;
            buf.data[idx] = val;
            buf.data[idx + 1] = val;
            buf.data[idx + 2] = val;
        }
    }
    return buf;
}

} // namespace

void run_phash_tests() {
    using namespace image_odb;
    using namespace image_odb::hash;

    // Test 1: Bit arithmetic and Hamming distance identity
    uint64_t hash_a = 0xAAAAAAAAAAAAAAAAULL;
    if (PHash::hamming_distance(hash_a, hash_a) != 0) {
        throw std::runtime_error("Self Hamming distance must be 0");
    }

    uint64_t hash_b = 0xAAAAAAAAAAAAAAABULL;
    if (PHash::hamming_distance(hash_a, hash_b) != 1) {
        throw std::runtime_error("Hamming distance of 1 bit difference failed");
    }

    // Test 2: Symmetry property
    if (PHash::hamming_distance(hash_a, hash_b) != PHash::hamming_distance(hash_b, hash_a)) {
        throw std::runtime_error("Hamming distance symmetry violation");
    }

    // Test 3: Same image hash consistency
    auto grad1 = make_gradient_image(100, 100);
    auto grad2 = make_gradient_image(200, 200); // Scaled version of same content
    uint64_t h_grad1 = PHash::compute(grad1);
    uint64_t h_grad2 = PHash::compute(grad2);
    uint32_t dist_scale = PHash::hamming_distance(h_grad1, h_grad2);
    if (dist_scale > 3) {
        throw std::runtime_error("Scale-invariance test failed, distance was: " + std::to_string(dist_scale));
    }

    // Test 4: Contrast/Brightness invariance
    auto grad_bright = make_gradient_image(100, 100);
    for (auto& b : grad_bright.data) {
        b = static_cast<uint8_t>(std::min(255, b + 20)); // Add uniform brightness offset
    }
    uint64_t h_grad_bright = PHash::compute(grad_bright);
    uint32_t dist_bright = PHash::hamming_distance(h_grad1, h_grad_bright);
    if (dist_bright > 3) {
        throw std::runtime_error("Brightness shift invariance test failed, distance was: " + std::to_string(dist_bright));
    }

    // Test 5: Distinct images have high Hamming distance
    auto checker = make_checkerboard_image(100, 100, 10);
    uint64_t h_checker = PHash::compute(checker);
    uint32_t dist_diff = PHash::hamming_distance(h_grad1, h_checker);
    if (dist_diff < 10) {
        throw std::runtime_error("Distinct patterns should have distance >= 10, got: " + std::to_string(dist_diff));
    }

    // Test 6: Empty buffer
    ImageBuffer empty_buf;
    if (PHash::compute(empty_buf) != 0) {
        throw std::runtime_error("Empty buffer must produce 0 hash");
    }
}
