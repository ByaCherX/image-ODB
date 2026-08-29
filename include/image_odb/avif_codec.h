#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <vector>
#include <span>

namespace image_odb::codec {

/**
 * @brief High-performance AVIF still-image and multi-frame inter-frame sequence codec.
 */
class AvifCodec {
public:
    /**
     * @brief Encode a single ImageBuffer into an AVIF still image file on disk using fine-grained options.
     * @param image Input image buffer.
     * @param output_path Output destination .avif file path.
     * @param options Encoding options (quality, speed, chroma subsampling, bit depth, lossless).
     * @return True if encoding and write succeeded.
     */
    static bool encode_still_image(const ImageBuffer& image,
                                   const std::filesystem::path& output_path,
                                   const EncodeOptions& options);

    /**
     * @brief Encode a single ImageBuffer into an AVIF still image file on disk (convenience overload).
     * @param image Input image (RGB8 or RGBA8).
     * @param output_path Output destination .avif file path.
     * @param quality Quality factor (1-100, default: 80).
     * @param speed Encoding speed trade-off (0-10, default: 6).
     * @return True if encoding and write succeeded.
     */
    static bool encode_still_image(const ImageBuffer& image,
                                   const std::filesystem::path& output_path,
                                   int quality = 80, int speed = 6);

    /**
     * @brief Encode multiple in-memory ImageBuffers into a single multi-frame AVIF container using fine-grained options.
     * @param frames List of image buffers in sequence.
     * @param output_path Output destination .avif file path.
     * @param options Encoding options.
     * @return True if encoding and write succeeded.
     */
    static bool encode_burst_sequence(const std::vector<ImageBuffer>& frames,
                                      const std::filesystem::path& output_path,
                                      const EncodeOptions& options);

    /**
     * @brief Encode multiple in-memory ImageBuffers into a single multi-frame AVIF container (convenience overload).
     * @param frames List of image buffers in sequence.
     * @param output_path Output destination .avif file path.
     * @param quality Quality factor (1-100, default: 80).
     * @param speed Encoding speed trade-off (0-10, default: 6).
     * @return True if encoding and write succeeded.
     */
    static bool encode_burst_sequence(const std::vector<ImageBuffer>& frames,
                                      const std::filesystem::path& output_path,
                                      int quality = 80, int speed = 6);

    /**
     * @brief Encode multiple image files from disk into a single multi-frame AVIF container with options.
     * @param input_files List of image file paths representing the burst sequence.
     * @param output_path Target .avif container path.
     * @param options Encoding options.
     * @return True if encoding succeeded.
     */
    static bool encode_burst_sequence(const std::vector<std::filesystem::path>& input_files,
                                      const std::filesystem::path& output_path,
                                      const EncodeOptions& options);

    /**
     * @brief Encode multiple image files from disk into a single multi-frame AVIF container (convenience overload).
     * @param input_files List of image file paths representing the burst sequence.
     * @param output_path Target .avif container path.
     * @param quality Compression quality (1-100, default 80).
     * @param speed Encoding speed trade-off (0-10, default 6).
     * @return True if encoding succeeded.
     */
    static bool encode_burst_sequence(const std::vector<std::filesystem::path>& input_files,
                                      const std::filesystem::path& output_path,
                                      int quality = 80, int speed = 6);

    /**
     * @brief Extract a single frame from an AVIF container file on disk.
     * @param avif_path Path to the AVIF container.
     * @param frame_index Zero-based frame index.
     * @param options Decoding options.
     * @return Decoded ImageBuffer of the requested frame, or empty buffer on failure.
     */
    static ImageBuffer extract_frame(const std::filesystem::path& avif_path,
                                     uint32_t frame_index,
                                     const DecodeOptions& options = {});

    /**
     * @brief Retrieve the total number of frames contained in an AVIF file.
     * @param avif_path Path to the AVIF container.
     * @return Total frame count (0 on error/invalid container).
     */
    static uint32_t get_frame_count(const std::filesystem::path& avif_path);
};

} // namespace image_odb::codec

