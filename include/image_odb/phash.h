#pragma once

#include "image_odb/core/types.h"
#include <cstdint>

namespace image_odb::hash {

/**
 * @brief Algorithmic implementation of 64-bit Discrete Cosine Transform (DCT) based Perceptual Hash (pHash).
 */
class PHash {
public:
    /**
     * @brief Compute 64-bit perceptual hash from a raw image buffer.
     * @param image Input image buffer (RGB, RGBA, or Grayscale).
     * @return 64-bit integer perceptual hash.
     */
    static uint64_t compute(const ImageBuffer& image);

    /**
     * @brief Compute the Hamming distance between two 64-bit perceptual hashes.
     * @param h1 First pHash value.
     * @param h2 Second pHash value.
     * @return Number of differing bits (0 to 64).
     */
    static uint32_t hamming_distance(uint64_t h1, uint64_t h2) noexcept;

    /**
     * @brief Check whether two images are visually similar within a given Hamming distance tolerance.
     * @param h1 First pHash value.
     * @param h2 Second pHash value.
     * @param max_distance Maximum allowed bit difference (default: 5).
     * @return True if distance <= max_distance.
     */
    static bool is_similar(uint64_t h1, uint64_t h2, uint32_t max_distance = 5) noexcept;
};

} // namespace image_odb::hash
