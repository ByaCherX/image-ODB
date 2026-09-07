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
     * @brief Decode an AVIF image from an in-memory byte slice.
     * @param data In-memory byte span containing AVIF data.
     * @param options Fine-grained decoding options.
     * @return Decoded ImageBuffer, or empty buffer on failure.
     */
    static ImageBuffer decode_memory(std::span<const uint8_t> data, const DecodeOptions& options = {});

    /**
     * @brief Encode a single ImageBuffer into an in-memory AVIF container.
     * @param image Input image buffer.
     * @param options Encoding options (quality, speed, chroma subsampling, bit depth, lossless).
     * @return Encoded AVIF byte vector.
     */
    static std::vector<uint8_t> encode_memory(const ImageBuffer& image, const EncodeOptions& options = {});

    /**
     * @brief Encode multiple in-memory ImageBuffers into a single multi-frame AVIF container in memory.
     * @param frames List of image buffers in sequence.
     * @param options Encoding options.
     * @return Encoded AVIF byte vector.
     */
    static std::vector<uint8_t> encode_burst_sequence(const std::vector<ImageBuffer>& frames,
                                                      const EncodeOptions& options = {});

    /**
     * @brief Extract a single frame from an AVIF container in memory.
     * @param data In-memory byte span containing AVIF container.
     * @param frame_index Zero-based frame index.
     * @param options Decoding options.
     * @return Decoded ImageBuffer of the requested frame, or empty buffer on failure.
     */
    static ImageBuffer extract_frame(std::span<const uint8_t> data,
                                     uint32_t frame_index = 0,
                                     const DecodeOptions& options = {});

    /**
     * @brief Retrieve the total number of frames contained in an AVIF container in memory.
     * @param data In-memory byte span containing AVIF container.
     * @return Total frame count (0 on error/invalid container).
     */
    static uint32_t get_frame_count(std::span<const uint8_t> data);
};

} // namespace image_odb::codec

