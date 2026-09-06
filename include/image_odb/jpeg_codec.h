#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <span>
#include <vector>

namespace image_odb::codec {

/**
 * @brief High-performance JPEG compression and decompression using libjpeg-turbo with exception-safe error handling.
 */
class JpegCodec {
public:
    /**
     * @brief Decode a JPEG buffer from an in-memory byte slice.
     * @param data In-memory byte span containing JPEG data.
     * @param options Fine-grained decoding options.
     * @return Decoded ImageBuffer, or empty buffer on corruption/failure.
     */
    static ImageBuffer decode_memory(std::span<const uint8_t> data, const DecodeOptions& options = {});

    /**
     * @brief Encode an in-memory ImageBuffer into a compressed JPEG memory buffer with options.
     * @param image Source buffer.
     * @param options Fine-grained encoding options.
     * @return Compressed JPEG byte vector.
     */
    static std::vector<uint8_t> encode_memory(const ImageBuffer& image, const EncodeOptions& options);

    /**
     * @brief Encode an in-memory ImageBuffer into a compressed JPEG memory buffer (convenience overload).
     * @param image Source buffer.
     * @param quality Compression quality factor (1-100, default: 85).
     * @return Compressed JPEG byte vector.
     */
    static std::vector<uint8_t> encode_memory(const ImageBuffer& image, int quality = 85);
};

} // namespace image_odb::codec

