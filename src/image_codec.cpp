#include "image_odb/image_codec.h"
#include "image_odb/jpeg_codec.h"
#include "image_odb/avif_codec.h"
#include "image_odb/png_codec.h"
#include "image_odb/webp_codec.h"
#include "image_odb/tiff_codec.h"
#include "image_odb/util.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace image_odb::codec {

namespace {

std::vector<uint8_t> read_file_bytes(const std::filesystem::path& file_path) {
    std::error_code ec;
    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec || file_size == 0) {
        if (ec) {
            spdlog::warn("Cannot determine file size for reading: {}", file_path.string());
        }
        return {};
    }

    std::ifstream infile(file_path, std::ios::binary);
    if (!infile.is_open()) {
        spdlog::warn("Cannot open file for reading: {}", file_path.string());
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    infile.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size));
    if (!infile.good() && !infile.eof()) {
        spdlog::warn("Failed to read all bytes from file: {}", file_path.string());
        return {};
    }

    return buffer;
}

bool write_file_bytes(const std::filesystem::path& path, const uint8_t* data, size_t size) {
    if (auto parent = path.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

} // namespace

ImageFormat ImageCodec::detect_format(const std::filesystem::path& file_path) {
    std::string ext = file_path.has_extension() ? file_path.extension().string() : file_path.string();
    if (ext.empty()) return ImageFormat::UNKNOWN;
    if (ext.front() == '.') ext.erase(0, 1);

    std::transform(
        ext.begin(), 
        ext.end(), 
        ext.begin(), 
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); }
    );

    if (ext == "jpg" || ext == "jpeg" || ext == "jfif") return ImageFormat::JPEG;
    if (ext == "avif" || ext == "avifs") return ImageFormat::AVIF;
    if (ext == "png") return ImageFormat::PNG;
    if (ext == "webp") return ImageFormat::WEBP;
    if (ext == "tif" || ext == "tiff") return ImageFormat::TIFF;
    if (ext == "bmp") return ImageFormat::BMP;
    return ImageFormat::UNKNOWN;
}

ImageFormat ImageCodec::detect_format(std::span<const uint8_t> data) {
    using namespace std::string_view_literals;
    using util::starts_with;

    if (starts_with(data, "\xFF\xD8")) return ImageFormat::JPEG;
    if (starts_with(data, "\x89PNG")) return ImageFormat::PNG;
    if (starts_with(data, "BM")) return ImageFormat::BMP;
    if (starts_with(data, "II\x2A\x00"sv) || starts_with(data, "MM\x00\x2A"sv)) return ImageFormat::TIFF;
    if (starts_with(data, "RIFF") && data.size() >= 12 && starts_with(data.subspan(8), "WEBP")) {
        return ImageFormat::WEBP;
    }
    if (data.size() >= 12 && starts_with(data.subspan(4), "ftyp")) {
        auto brand = data.subspan(8, 4);
        if (starts_with(brand, "avif") || starts_with(brand, "avis") ||
            starts_with(brand, "mif1") || starts_with(brand, "msf1")) {
            return ImageFormat::AVIF;
        }
        for (size_t offset = 16; offset + 4 <= data.size() && offset < 64; offset += 4) {
            auto comp_brand = data.subspan(offset, 4);
            if (starts_with(comp_brand, "avif") || starts_with(comp_brand, "avis")) {
                return ImageFormat::AVIF;
            }
        }
        return ImageFormat::AVIF; // Fallback to AVIF for any ftyp container
    }
    return ImageFormat::UNKNOWN;
}

ImageBuffer ImageCodec::decode_file(const std::filesystem::path& file_path, const DecodeOptions& options) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) {
        return {};
    }

    // Attempt format detection from magic bytes first to avoid extension string allocation
    ImageFormat fmt = detect_format(file_bytes);
    if (fmt != ImageFormat::UNKNOWN) {
        return decode_memory(file_bytes, "", options);
    }

    return decode_memory(file_bytes, file_path.extension().string(), options);
}

ImageBuffer ImageCodec::decode_memory(std::span<const uint8_t> data, std::string_view hint_format, const DecodeOptions& options) {
    if (data.empty()) return {};

    ImageFormat fmt = detect_format(data);
    if (fmt == ImageFormat::UNKNOWN && !hint_format.empty()) {
        fmt = detect_format(std::filesystem::path(hint_format));
    }

    switch (fmt) {
        case ImageFormat::JPEG:
            return JpegCodec::decode_memory(data, options);
        case ImageFormat::AVIF:
            return AvifCodec::decode_memory(data, options);
        case ImageFormat::PNG:
            return PngCodec::decode_memory(data, options);
        case ImageFormat::WEBP:
            return WebpCodec::decode_memory(data, options);
        case ImageFormat::TIFF:
            return TiffCodec::decode_memory(data, options);
        default:
            return {};
    }
}

std::vector<uint8_t> ImageCodec::encode_memory(const ImageBuffer& image, const EncodeOptions& options) {
    if (image.empty()) return {};

    switch (options.format) {
        case ImageFormat::AVIF:
            return AvifCodec::encode_memory(image, options);
        case ImageFormat::JPEG:
        default:
            return JpegCodec::encode_memory(image, options);
    }
}

bool ImageCodec::encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, const EncodeOptions& options) {
    if (image.empty()) return false;

    auto bytes = encode_memory(image, options);
    if (bytes.empty()) return false;

    return write_file_bytes(output_path, bytes.data(), bytes.size());
}

bool ImageCodec::encode_burst_file(const std::vector<ImageBuffer>& frames,
                                  const std::filesystem::path& output_path,
                                  const EncodeOptions& options) {
    if (frames.empty()) return false;

    auto bytes = AvifCodec::encode_burst_sequence(frames, options);
    if (bytes.empty()) return false;

    return write_file_bytes(output_path, bytes.data(), bytes.size());
}

ImageBuffer ImageCodec::extract_frame(std::span<const uint8_t> data,
                                     uint32_t frame_index,
                                     const DecodeOptions& options) {
    if (data.empty()) return {};
    return AvifCodec::extract_frame(data, frame_index, options);
}

ImageBuffer ImageCodec::extract_frame(const std::filesystem::path& file_path,
                                     uint32_t frame_index,
                                     const DecodeOptions& options) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) return {};

    return extract_frame(file_bytes, frame_index, options);
}

uint32_t ImageCodec::get_frame_count(std::span<const uint8_t> data) {
    if (data.empty()) return 0;
    return AvifCodec::get_frame_count(data);
}

/* DEPRACATED */
uint32_t ImageCodec::get_frame_count(const std::filesystem::path& file_path) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) return 0;

    return get_frame_count(file_bytes);
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

    ImageBuffer dst;
    dst.width = dst_w;
    dst.height = dst_h;
    dst.channels = src.channels;
    dst.format = src.format;
    dst.color_profile = src.color_profile;
    
    size_t bytes_per_pixel = src.channels * (src.bit_depth() / 8);
    dst.data.resize(static_cast<size_t>(dst_w) * dst_h * bytes_per_pixel);

    // Precalculate horizontal mapping (LUT) to eliminate floating-point division from the inner loop
    double inv_scale = 1.0 / scale;
    std::vector<uint32_t> lut_src_x(dst_w);
    for (uint32_t x = 0; x < dst_w; ++x) {
        lut_src_x[x] = std::min(src.width - 1, static_cast<uint32_t>(x * inv_scale));
    }

    // Area/Nearest-neighbor downsampling copying full pixel byte spans directly
    const size_t src_stride = static_cast<size_t>(src.width) * bytes_per_pixel;
    const size_t dst_stride = static_cast<size_t>(dst_w) * bytes_per_pixel;

    for (uint32_t y = 0; y < dst_h; ++y) {
        uint32_t src_y = std::min(src.height - 1, static_cast<uint32_t>(y * inv_scale));
        const uint8_t* src_row = src.data.data() + static_cast<size_t>(src_y) * src_stride;
        uint8_t* dst_row = dst.data.data() + static_cast<size_t>(y) * dst_stride;

        for (uint32_t x = 0; x < dst_w; ++x) {
            const uint8_t* src_pixel = src_row + static_cast<size_t>(lut_src_x[x]) * bytes_per_pixel;
            uint8_t* dst_pixel = dst_row + static_cast<size_t>(x) * bytes_per_pixel;
            std::memcpy(dst_pixel, src_pixel, bytes_per_pixel);
        }
    }

    return dst;
}

} // namespace image_odb::codec
