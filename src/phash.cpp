#include "image_odb/phash.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <bit>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace image_odb::hash {

namespace {

constexpr uint32_t SAMPLE_SIZE = 32;
constexpr uint32_t DCT_SIZE = 8;

/**
 * @brief Downsample image to 32x32 luminance matrix using area-averaging (box filter).
 */
std::vector<double> downsample_to_32x32(const ImageBuffer& img) {
    std::vector<double> matrix(SAMPLE_SIZE * SAMPLE_SIZE, 0.0);
    if (img.empty() || img.width == 0 || img.height == 0) return matrix;

    for (uint32_t by = 0; by < SAMPLE_SIZE; ++by) {
        uint32_t y_start = by * img.height / SAMPLE_SIZE;
        uint32_t y_end = std::max(y_start + 1, (by + 1) * img.height / SAMPLE_SIZE);
        y_end = std::min(y_end, img.height);

        for (uint32_t bx = 0; bx < SAMPLE_SIZE; ++bx) {
            uint32_t x_start = bx * img.width / SAMPLE_SIZE;
            uint32_t x_end = std::max(x_start + 1, (bx + 1) * img.width / SAMPLE_SIZE);
            x_end = std::min(x_end, img.width);

            double sum_luma = 0.0;
            uint32_t count = 0;

            for (uint32_t py = y_start; py < y_end; ++py) {
                for (uint32_t px = x_start; px < x_end; ++px) {
                    size_t idx = (py * img.width + px) * img.channels;
                    if (idx < img.data.size()) {
                        double luma;
                        if (img.channels >= 3) {
                            luma = 0.299 * img.data[idx] + 0.587 * img.data[idx + 1] + 0.114 * img.data[idx + 2];
                        } else {
                            luma = static_cast<double>(img.data[idx]);
                        }
                        sum_luma += luma;
                        count++;
                    }
                }
            }

            matrix[by * SAMPLE_SIZE + bx] = (count > 0) ? (sum_luma / count) : 0.0;
        }
    }
    return matrix;
}

} // namespace

uint64_t PHash::compute(const ImageBuffer& image) {
    if (image.empty()) return 0;

    // 1. Downsampling to 32x32 area-averaged luminance matrix
    auto matrix = downsample_to_32x32(image);

    // 2. 2D Discrete Cosine Transform (DCT-II) for top-left 8x8 coefficients
    std::vector<double> dct(DCT_SIZE * DCT_SIZE, 0.0);
    for (uint32_t u = 0; u < DCT_SIZE; ++u) {
        for (uint32_t v = 0; v < DCT_SIZE; ++v) {
            double sum = 0.0;
            for (uint32_t i = 0; i < SAMPLE_SIZE; ++i) {
                for (uint32_t j = 0; j < SAMPLE_SIZE; ++j) {
                    sum += matrix[i * SAMPLE_SIZE + j] *
                           std::cos(((2 * i + 1) * u * M_PI) / (2.0 * SAMPLE_SIZE)) *
                           std::cos(((2 * j + 1) * v * M_PI) / (2.0 * SAMPLE_SIZE));
                }
            }
            double cu = (u == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
            double cv = (v == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
            dct[u * DCT_SIZE + v] = (2.0 / SAMPLE_SIZE) * cu * cv * sum;
        }
    }

    // 3. Compute mean of top-left 8x8 excluding DC term (0,0)
    double sum = 0.0;
    int count = 0;
    for (uint32_t u = 0; u < DCT_SIZE; ++u) {
        for (uint32_t v = 0; v < DCT_SIZE; ++v) {
            if (u == 0 && v == 0) continue; // Exclude DC component
            sum += dct[u * DCT_SIZE + v];
            count++;
        }
    }
    double mean = (count > 0) ? (sum / count) : 0.0;

    // 4. Construct 64-bit hash bitmask
    uint64_t hash = 0;
    int bit_idx = 0;
    for (uint32_t u = 0; u < DCT_SIZE; ++u) {
        for (uint32_t v = 0; v < DCT_SIZE; ++v) {
            if (u == 0 && v == 0) continue;
            if (dct[u * DCT_SIZE + v] > mean) {
                hash |= (1ULL << (62 - bit_idx));
            }
            bit_idx++;
        }
    }

    spdlog::debug("PHash::compute: calculated pHash 0x{:016x} for image {}x{}", hash, image.width, image.height);
    return hash;
}

uint32_t PHash::hamming_distance(uint64_t h1, uint64_t h2) noexcept {
    return static_cast<uint32_t>(std::popcount(h1 ^ h2));
}

bool PHash::is_similar(uint64_t h1, uint64_t h2, uint32_t max_distance) noexcept {
    return hamming_distance(h1, h2) <= max_distance;
}

} // namespace image_odb::hash
