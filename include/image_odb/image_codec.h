#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <span>

namespace image_odb::codec {

/**
 * @brief Canonical list of supported image file extensions for discovery, indexing, and codecs.
 */
inline constexpr std::array<std::string_view, 10> SUPPORTED_IMAGE_EXTENSIONS = {
    ".jpg", ".jpeg", ".jfif", ".avif", ".avifs", ".png", ".webp", ".tif", ".tiff", ".bmp"
};

/**
 * @brief Supported image file extensions that currently support direct decoding.
 */
inline constexpr std::array<std::string_view, 6> SUPPORTED_DECODE_EXTENSIONS = {
    ".jpg", ".jpeg", ".jfif", ".avif", ".avifs", ".png"
};

/**
 * @brief Supported image container formats for encoding.
 */
inline constexpr std::array<ImageFormat, 2> SUPPORTED_ENCODE_FORMATS = {
    ImageFormat::AVIF, ImageFormat::JPEG
};

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
     * @brief Encode an in-memory buffer into compressed image bytes with fine-grained options.
     * @param image Source image buffer.
     * @param options Fine-grained encoding options.
     * @return Compressed byte vector.
     */
    static std::vector<uint8_t> encode_memory(const ImageBuffer& image,
                                              const EncodeOptions& options = {});

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
     * @brief Encode multiple frames into an AVIF burst sequence file on disk.
     * @param frames Frames to encode.
     * @param output_path Target file path.
     * @param options Encoding options.
     * @return True if saved successfully.
     */
    static bool encode_burst_file(const std::vector<ImageBuffer>& frames,
                                  const std::filesystem::path& output_path,
                                  const EncodeOptions& options = {});

    /**
     * @brief Extract a specific frame from an image or container file on disk.
     * @param file_path File path to image container.
     * @param frame_index Zero-based frame index.
     * @param options Decoding options.
     * @return Decoded ImageBuffer.
     */
    static ImageBuffer extract_frame(const std::filesystem::path& file_path,
                                     uint32_t frame_index = 0,
                                     const DecodeOptions& options = {});

    /**
     * @brief Retrieve frame count from an image container file on disk.
     * @param file_path File path to image container.
     * @return Total frame count.
     */
    static uint32_t get_frame_count(const std::filesystem::path& file_path);

    /**
     * @brief Downscale/resize an image to fit within target bounding box preserving aspect ratio.
     * @param src Source image.
     * @param max_width Maximum allowable width.
     * @param max_height Maximum allowable height.
     * @return Resized ImageBuffer.
     */
    static ImageBuffer resize_aspect_fit(const ImageBuffer& src, uint32_t max_width, uint32_t max_height);

    /**
     * @brief Detect image container format from file extension.
     */
    static ImageFormat detect_format(const std::filesystem::path& file_path);

    /**
     * @brief Detect image format from magic bytes in header buffer.
     */
    static ImageFormat detect_format(std::span<const uint8_t> data);
};

} // namespace image_odb::codec
