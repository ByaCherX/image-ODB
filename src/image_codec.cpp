#include "image_odb/image_codec.h"
#include "image_odb/jpeg_codec.h"
#include "image_odb/avif_codec.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace image_odb::codec {

ImageFormat ImageCodec::detect_format(const std::filesystem::path& file_path) {
    auto ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".jfif") return ImageFormat::JPEG;
    if (ext == ".avif" || ext == ".avifs") return ImageFormat::AVIF;
    if (ext == ".png") return ImageFormat::PNG;
    if (ext == ".webp") return ImageFormat::WEBP;
    if (ext == ".tif" || ext == ".tiff") return ImageFormat::TIFF;
    if (ext == ".bmp") return ImageFormat::BMP;
    return ImageFormat::UNKNOWN;
}

ImageFormat ImageCodec::detect_format(std::span<const uint8_t> data) {
    if (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8) return ImageFormat::JPEG;
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return ImageFormat::PNG;
    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') return ImageFormat::WEBP;
    if (data.size() >= 12 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p') {
        return ImageFormat::AVIF;
    }
    return ImageFormat::UNKNOWN;
}

ImageBuffer ImageCodec::decode_file(const std::filesystem::path& file_path, const DecodeOptions& options) {
    auto fmt = detect_format(file_path);
    spdlog::debug("ImageCodec::decode_file: decoding '{}' (format={})", file_path.string(), static_cast<int>(fmt));

    switch (fmt) {
        case ImageFormat::JPEG:
            return JpegCodec::decode_file(file_path, options);
        case ImageFormat::AVIF:
            return AvifCodec::extract_frame(file_path, 0, options);
        case ImageFormat::PNG:
        case ImageFormat::WEBP:
        case ImageFormat::TIFF:
        case ImageFormat::BMP:
            spdlog::warn("Decoding support for {} will be available in future codec extensions: {}",
                         file_path.extension().string(), file_path.string());
            return {};
        default:
            spdlog::warn("Unsupported image format for decoding: {}", file_path.string());
            return {};
    }
}

ImageBuffer ImageCodec::decode_memory(std::span<const uint8_t> data, const std::string& hint_format, const DecodeOptions& options) {
    (void)hint_format;
    auto fmt = detect_format(data);
    if (fmt == ImageFormat::JPEG) {
        return JpegCodec::decode_memory(data, options);
    }
    return {};
}

bool ImageCodec::encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, const EncodeOptions& options) {
    if (image.empty()) return false;

    spdlog::debug("ImageCodec::encode_file: encoding to '{}' (format={}, quality={})",
                  output_path.string(), options.format == ImageFormat::AVIF ? "AVIF" : "JPEG", options.quality);

    if (options.format == ImageFormat::AVIF) {
        return AvifCodec::encode_still_image(image, output_path, options);
    } else if (options.format == ImageFormat::JPEG) {
        return JpegCodec::encode_file(image, output_path, options);
    }
    
    // Default fallback to JPEG
    return JpegCodec::encode_file(image, output_path, options);
}

bool ImageCodec::encode_file(const ImageBuffer& image, const std::filesystem::path& output_path,
                             ImageFormat format, int quality) {
    EncodeOptions options;
    options.format = format;
    options.quality = quality;
    return encode_file(image, output_path, options);
}

ImageBuffer ImageCodec::resize_aspect_fit(const ImageBuffer& src, uint32_t max_width, uint32_t max_height) {
    if (src.empty() || max_width == 0 || max_height == 0) return {};

    double scale = std::min(static_cast<double>(max_width) / src.width,
                            static_cast<double>(max_height) / src.height);
    if (scale >= 1.0) {
        return src; // No downsizing required
    }

    uint32_t dst_w = std::max<uint32_t>(1, static_cast<uint32_t>(std::round(src.width * scale)));
    uint32_t dst_h = std::max<uint32_t>(1, static_cast<uint32_t>(std::round(src.height * scale)));
    spdlog::debug("ImageCodec::resize_aspect_fit: resizing {}x{} -> {}x{} (scale={:.3f})",
                  src.width, src.height, dst_w, dst_h, scale);

    ImageBuffer dst;
    dst.width = dst_w;
    dst.height = dst_h;
    dst.channels = src.channels;
    dst.format = src.format;
    dst.color_profile = src.color_profile;
    
    size_t bytes_per_pixel = src.channels * (src.bit_depth() / 8);
    dst.data.resize(static_cast<size_t>(dst_w) * dst_h * bytes_per_pixel);

    // Nearest-neighbor / area mapping downsampling
    if (src.bit_depth() == 16) {
        const uint16_t* src_ptr16 = reinterpret_cast<const uint16_t*>(src.data.data());
        uint16_t* dst_ptr16 = reinterpret_cast<uint16_t*>(dst.data.data());
        for (uint32_t y = 0; y < dst_h; ++y) {
            uint32_t src_y = std::min(src.height - 1, static_cast<uint32_t>(y / scale));
            for (uint32_t x = 0; x < dst_w; ++x) {
                uint32_t src_x = std::min(src.width - 1, static_cast<uint32_t>(x / scale));
                for (uint32_t c = 0; c < src.channels; ++c) {
                    dst_ptr16[(y * dst_w + x) * src.channels + c] = 
                        src_ptr16[(src_y * src.width + src_x) * src.channels + c];
                }
            }
        }
    } else {
        for (uint32_t y = 0; y < dst_h; ++y) {
            uint32_t src_y = std::min(src.height - 1, static_cast<uint32_t>(y / scale));
            for (uint32_t x = 0; x < dst_w; ++x) {
                uint32_t src_x = std::min(src.width - 1, static_cast<uint32_t>(x / scale));
                for (uint32_t c = 0; c < src.channels; ++c) {
                    dst.data[(y * dst_w + x) * src.channels + c] = 
                        src.data[(src_y * src.width + src_x) * src.channels + c];
                }
            }
        }
    }

    return dst;
}

} // namespace image_odb::codec
