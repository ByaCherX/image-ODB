#include "image_odb/webp_codec.h"
#include "image_odb/image_codec.h"
#include <webp/decode.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace image_odb::codec {

ImageBuffer WebpCodec::decode_memory(std::span<const uint8_t> data, const DecodeOptions& options) {
    if (data.empty()) return {};

    WebPBitstreamFeatures features{};
    VP8StatusCode status = WebPGetFeatures(data.data(), data.size(), &features);
    if (status != VP8_STATUS_OK) {
        spdlog::warn("WebpCodec: Failed to get WebP features (status: {})", static_cast<int>(status));
        return {};
    }

    // Determine target format
    PixelFormat target_format = PixelFormat::RGB8;
    uint32_t channels = 3;
    bool convert_to_gray = false;

    if (options.target_format.has_value()) {
        switch (*options.target_format) {
            case PixelFormat::GRAY8:
                target_format = PixelFormat::RGB8;
                channels = 3;
                convert_to_gray = true;
                break;
            case PixelFormat::RGBA8:
                target_format = PixelFormat::RGBA8;
                channels = 4;
                break;
            case PixelFormat::RGB8:
            default:
                target_format = PixelFormat::RGB8;
                channels = 3;
                break;
        }
    } else {
        if (features.has_alpha) {
            target_format = PixelFormat::RGBA8;
            channels = 4;
        } else {
            target_format = PixelFormat::RGB8;
            channels = 3;
        }
    }

    ImageBuffer buffer;
    buffer.width = static_cast<uint32_t>(features.width);
    buffer.height = static_cast<uint32_t>(features.height);
    buffer.channels = channels;
    buffer.format = target_format;
    buffer.color_profile.primaries = ColorPrimaries::BT709_sRGB;
    buffer.color_profile.transfer = TransferCharacteristics::sRGB;

    size_t row_stride = static_cast<size_t>(buffer.width) * channels;
    size_t total_size = row_stride * buffer.height;
    buffer.data.resize(total_size);

    uint8_t* decode_res = nullptr;
    if (channels == 4) {
        decode_res = WebPDecodeRGBAInto(data.data(), data.size(),
                                       buffer.data.data(), total_size,
                                       static_cast<int>(row_stride));
    } else {
        decode_res = WebPDecodeRGBInto(data.data(), data.size(),
                                      buffer.data.data(), total_size,
                                      static_cast<int>(row_stride));
    }

    if (!decode_res) {
        spdlog::warn("WebpCodec: Failed to decode WebP image data");
        return {};
    }

    // Convert to grayscale if requested
    if (convert_to_gray) {
        std::vector<uint8_t> gray_data(static_cast<size_t>(buffer.width) * buffer.height);
        for (size_t i = 0; i < gray_data.size(); ++i) {
            uint8_t r = buffer.data[i * 3 + 0];
            uint8_t g = buffer.data[i * 3 + 1];
            uint8_t b = buffer.data[i * 3 + 2];
            gray_data[i] = static_cast<uint8_t>((299u * r + 587u * g + 114u * b + 500u) / 1000u);
        }
        buffer.data = std::move(gray_data);
        buffer.channels = 1;
        buffer.format = PixelFormat::GRAY8;
    }

    // Extract EXIF chunk from RIFF container if present
    if (data.size() >= 12 &&
        std::memcmp(data.data(), "RIFF", 4) == 0 &&
        std::memcmp(data.data() + 8, "WEBP", 4) == 0) {
        size_t offset = 12;
        while (offset + 8 <= data.size()) {
            const uint8_t* chunk_header = data.data() + offset;
            uint32_t chunk_len = static_cast<uint32_t>(chunk_header[4]) |
                                 (static_cast<uint32_t>(chunk_header[5]) << 8) |
                                 (static_cast<uint32_t>(chunk_header[6]) << 16) |
                                 (static_cast<uint32_t>(chunk_header[7]) << 24);
            size_t chunk_data_offset = offset + 8;
            if (chunk_data_offset + chunk_len > data.size()) {
                break;
            }
            if (std::memcmp(chunk_header, "EXIF", 4) == 0 && chunk_len > 0) {
                buffer.exif_data.assign(data.data() + chunk_data_offset,
                                        data.data() + chunk_data_offset + chunk_len);
                break;
            }
            offset = chunk_data_offset + chunk_len + (chunk_len & 1);
        }
    }

    // Downscale if requested
    if (options.downscale_factor > 1) {
        uint32_t target_w = std::max(1u, buffer.width / options.downscale_factor);
        uint32_t target_h = std::max(1u, buffer.height / options.downscale_factor);
        buffer = ImageCodec::resize_aspect_fit(buffer, target_w, target_h);
    }

    return buffer;
}

} // namespace image_odb::codec

