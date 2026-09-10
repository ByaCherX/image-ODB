#include "image_odb/png_codec.h"
#include "image_odb/image_codec.h"
#include <spng.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <memory>

namespace image_odb::codec {

ImageBuffer PngCodec::decode_memory(std::span<const uint8_t> data, const DecodeOptions& options) {
    if (data.empty()) return {};

    std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)> ctx(spng_ctx_new(0), spng_ctx_free);
    if (!ctx) {
        spdlog::warn("PngCodec: Failed to create spng context");
        return {};
    }

    int ret = spng_set_png_buffer(ctx.get(), data.data(), data.size());
    if (ret != 0) {
        spdlog::warn("PngCodec: Failed to set png buffer: {}", spng_strerror(ret));
        return {};
    }

    spng_ihdr ihdr{};
    ret = spng_get_ihdr(ctx.get(), &ihdr);
    if (ret != 0) {
        spdlog::warn("PngCodec: Failed to get IHDR: {}", spng_strerror(ret));
        return {};
    }

    // Determine target format
    int spng_fmt = SPNG_FMT_RGB8; 
    PixelFormat target_format = PixelFormat::RGB8;
    uint32_t channels = 3;
    bool convert_to_gray = false;

    if (options.target_format.has_value()) {
        switch (*options.target_format) {
            case PixelFormat::GRAY8:
                if (ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE) {
                    spng_fmt = SPNG_FMT_G8;
                    target_format = PixelFormat::GRAY8;
                    channels = 1;
                } else {
                    // libspng only supports SPNG_FMT_G8 for native grayscale PNGs.
                    // For color PNGs, decode to RGB8 first then convert to grayscale.
                    spng_fmt = SPNG_FMT_RGB8;
                    target_format = PixelFormat::RGB8;
                    channels = 3;
                    convert_to_gray = true;
                }
                break;
            case PixelFormat::RGBA8:
                spng_fmt = SPNG_FMT_RGBA8;
                target_format = PixelFormat::RGBA8;
                channels = 4;
                break;
            case PixelFormat::RGB8:
            default:
                spng_fmt = SPNG_FMT_RGB8;
                target_format = PixelFormat::RGB8;
                channels = 3;
                break;
        }
    } else {
        // Automatic format selection based on PNG color type
        if (ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA ||
            ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA) {
            spng_fmt = SPNG_FMT_RGBA8;
            target_format = PixelFormat::RGBA8;
            channels = 4;
        } else if (ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE) {
            spng_fmt = SPNG_FMT_G8;
            target_format = PixelFormat::GRAY8;
            channels = 1;
        } else {
            spng_fmt = SPNG_FMT_RGB8;
            target_format = PixelFormat::RGB8;
            channels = 3;
        }
    }

    size_t out_size = 0;
    ret = spng_decoded_image_size(ctx.get(), spng_fmt, &out_size);
    if (ret != 0 || out_size == 0) {
        spdlog::warn("PngCodec: Failed to determine decoded image size: {}", spng_strerror(ret));
        return {};
    }

    ImageBuffer buffer;
    buffer.width = ihdr.width;
    buffer.height = ihdr.height;
    buffer.channels = channels;
    buffer.format = target_format;
    buffer.color_profile.primaries = ColorPrimaries::BT709_sRGB;
    buffer.color_profile.transfer = TransferCharacteristics::sRGB;
    buffer.data.resize(out_size);

    ret = spng_decode_image(ctx.get(), buffer.data.data(), out_size, spng_fmt, 0);
    if (ret != 0) {
        spdlog::warn("PngCodec: Failed to decode image: {}", spng_strerror(ret));
        return {};
    }

    // Convert to grayscale if requested on a color PNG
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

    // Try extracting EXIF metadata if present in PNG
    spng_exif exif{};
    if (spng_get_exif(ctx.get(), &exif) == 0 && exif.length > 0) {
        buffer.exif_data.assign(exif.data, exif.data + exif.length);
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
