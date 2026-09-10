#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <span>
#include <vector>

namespace image_odb::codec {

/**
 * @brief High-performance, lightweight TIFF decompression using libtiff.
 * Only decode support is implemented to minimize binary footprint.
 */
class TiffCodec {
public:
    /**
     * @brief Decode a TIFF buffer from an in-memory byte slice.
     * @param data In-memory byte span containing TIFF data.
     * @param options Fine-grained decoding options.
     * @return Decoded ImageBuffer, or empty buffer on corruption/failure.
     */
    static ImageBuffer decode_memory(std::span<const uint8_t> data, const DecodeOptions& options = {});
};

} // namespace image_odb::codec

