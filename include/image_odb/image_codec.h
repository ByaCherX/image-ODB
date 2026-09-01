#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <span>

namespace image_odb::codec {

/**
 * @brief Unified interface for decoding and encoding different image formats.
 */
class ImageCodec {
public:
    /**
     * @brief Decode an image from disk into an in-memory RGB/RGBA buffer.
     * @param file_path File path to the image.
     * @param options Decoding options (target format, downscaling factor).
     * @return Decoded ImageBuffer, or empty buffer on failure.
     */
    static ImageBuffer decode_file(const std::filesystem::path& file_path,
                                   const DecodeOptions& options = {});

    /**
     * @brief Decode an image from an in-memory byte slice.
     * @param data Byte slice containing encoded image data.
     * @param hint_format Optional format hint (e.g., "jpeg", "avif", "png").
     * @param options Decoding options.
     * @return Decoded ImageBuffer, or empty buffer on failure.
     */
    static ImageBuffer decode_memory(std::span<const uint8_t> data,
                                     const std::string& hint_format = "",
                                     const DecodeOptions& options = {});

    /**
     * @brief Encode an in-memory buffer to disk with fine-grained options.
     * @param image Source image buffer.
     * @param output_path Target file path.
     * @param options Fine-grained encoding options (format, quality, speed, subsampling, bit depth, lossless).
     * @return True if saved successfully.
     */
    static bool encode_file(const ImageBuffer& image,
                            const std::filesystem::path& output_path,
                            const EncodeOptions& options);

    /**
     * @brief Encode an in-memory buffer to disk in the specified format (convenience overload).
     * @param image Source image buffer.
     * @param output_path Target file path.
     * @param format Desired output container format.
     * @param quality Compression quality (1-100).
     * @return True if saved successfully.
     */
    static bool encode_file(const ImageBuffer& image,
                            const std::filesystem::path& output_path,
                            ImageFormat format = ImageFormat::AVIF,
                            int quality = 80);

    /**
     * @brief Downscale/resize an image to fit within target bounding box preserving aspect ratio.
     * @param src Source image.
     * @param max_width Maximum allowable width.
     * @param max_height Maximum allowable height.
     * @return Resized ImageBuffer.
     */
    static ImageBuffer resize_aspect_fit(const ImageBuffer& src, uint32_t max_width, uint32_t max_height);
};

} // namespace image_odb::codec
