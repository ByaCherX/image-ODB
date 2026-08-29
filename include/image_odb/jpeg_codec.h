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
     * @brief Decode a JPEG file from disk to an RGB/Grayscale ImageBuffer.
     * @param file_path Path to the JPEG file on disk.
     * @param options Fine-grained decoding options (downscaling, target format).
     * @return Decoded ImageBuffer, or empty buffer on corruption/failure.
     */
    static ImageBuffer decode_file(const std::filesystem::path& file_path, const DecodeOptions& options = {});

    /**
     * @brief Decode a JPEG buffer from an in-memory byte slice.
     * @param data In-memory byte span containing JPEG data.
     * @param options Fine-grained decoding options.
     * @return Decoded ImageBuffer, or empty buffer on corruption/failure.
     */
    static ImageBuffer decode_memory(std::span<const uint8_t> data, const DecodeOptions& options = {});

    /**
     * @brief Encode an in-memory ImageBuffer to a JPEG file on disk with options.
     * @param image Source buffer (RGB8, RGBA8, or GRAY8).
     * @param output_path Output destination file path.
     * @param options Fine-grained encoding options (quality, subsampling).
     * @return True if encoding and writing succeeded.
     */
    static bool encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, const EncodeOptions& options);

    /**
     * @brief Encode an in-memory ImageBuffer to a JPEG file on disk (convenience overload).
     * @param image Source buffer (RGB8, RGBA8, or GRAY8).
     * @param output_path Output destination file path.
     * @param quality Compression quality factor (1-100, default: 85).
     * @return True if encoding and writing succeeded.
     */
    static bool encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, int quality = 85);

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

