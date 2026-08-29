#pragma once

#include "image_odb/core/types.h"
#include <string>
#include <vector>
#include <span>

namespace image_odb::hash {

/**
 * @brief Represents decoded ThumbHash metadata and approximate aspect ratio.
 */
struct ThumbHashInfo {
    float approximate_aspect_ratio{1.0f};
    bool has_alpha{false};
};

/**
 * @brief Pure C++ implementation of ThumbHash visual placeholder algorithm.
 */
class ThumbHash {
public:
    /**
     * @brief Encode an image buffer into a binary ThumbHash payload (approx. 21-25 bytes).
     * @param image Input image (RGB or RGBA).
     * @return Binary ThumbHash payload.
     */
    static std::vector<uint8_t> encode(const ImageBuffer& image);

    /**
     * @brief Encode an image into a Base64-encoded ThumbHash string.
     * @param image Input image.
     * @return Base64 ThumbHash representation.
     */
    static std::string encode_to_base64(const ImageBuffer& image);

    /**
     * @brief Decode a binary ThumbHash payload back into an RGBA preview buffer.
     * @param hash_bytes Binary ThumbHash payload.
     * @param target_width Optional target width (0 = auto based on aspect ratio).
     * @param target_height Optional target height (0 = auto based on aspect ratio).
     * @return Decoded RGBA image buffer.
     */
    static ImageBuffer decode(std::span<const uint8_t> hash_bytes,
                              uint32_t target_width = 32,
                              uint32_t target_height = 32);

    /**
     * @brief Decode a Base64 ThumbHash string back into an RGBA preview buffer.
     * @param base64_hash Base64 ThumbHash string.
     * @param target_width Target preview width.
     * @param target_height Target preview height.
     * @return Decoded RGBA image buffer.
     */
    static ImageBuffer decode_from_base64(const std::string& base64_hash,
                                          uint32_t target_width = 32,
                                          uint32_t target_height = 32);

    /**
     * @brief Extract image aspect ratio and alpha flag from a ThumbHash payload without full decoding.
     * @param hash_bytes Binary ThumbHash payload.
     * @return ThumbHashInfo with approximate aspect ratio and alpha flag.
     */
    static ThumbHashInfo extract_info(std::span<const uint8_t> hash_bytes);
};

} // namespace image_odb::hash
