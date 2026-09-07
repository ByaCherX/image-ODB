#include "image_odb/image_codec.h"
#include "image_odb/jpeg_codec.h"
#include "image_odb/avif_codec.h"
#include "image_odb/util.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace image_odb::codec {

namespace {

std::vector<uint8_t> read_file_bytes(const std::filesystem::path& file_path) {
    std::ifstream infile(file_path, std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        spdlog::warn("Cannot open file for reading: {}", file_path.string());
        return {};
    }

    auto file_size = infile.tellg();
    if (file_size <= 0) return {};

    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    infile.seekg(0, std::ios::beg);
    infile.read(reinterpret_cast<char*>(buffer.data()), file_size);
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
    auto ext = file_path.extension().string();
    std::transform(
        ext.begin(), 
        ext.end(), 
        ext.begin(), 
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); }
    );
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".jfif") return ImageFormat::JPEG;
    if (ext == ".avif" || ext == ".avifs") return ImageFormat::AVIF;
    if (ext == ".png") return ImageFormat::PNG;
    if (ext == ".webp") return ImageFormat::WEBP;
    if (ext == ".tif" || ext == ".tiff") return ImageFormat::TIFF;
    if (ext == ".bmp") return ImageFormat::BMP;
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
        return ImageFormat::AVIF;
    }
    return ImageFormat::UNKNOWN;
}

ImageBuffer ImageCodec::decode_file(const std::filesystem::path& file_path, const DecodeOptions& options) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) {
        return {};
    }
    auto ext = file_path.extension().string();
    return decode_memory(file_bytes, ext, options);
}

ImageBuffer ImageCodec::decode_memory(std::span<const uint8_t> data, const std::string& hint_format, const DecodeOptions& options) {
    if (data.empty()) return {};

    ImageFormat fmt = detect_format(data);
    if (fmt == ImageFormat::UNKNOWN && !hint_format.empty()) {
        std::string h = hint_format;
        std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (h == ".jpg" || h == ".jpeg" || h == ".jfif" || h == "jpg" || h == "jpeg") fmt = ImageFormat::JPEG;
        else if (h == ".avif" || h == ".avifs" || h == "avif") fmt = ImageFormat::AVIF;
    }

    switch (fmt) {
        case ImageFormat::JPEG:
            return JpegCodec::decode_memory(data, options);
        case ImageFormat::AVIF:
            return AvifCodec::decode_memory(data, options);
        default:
            spdlog::warn("Unsupported image format for in-memory decoding");
            return {};
    }
}

std::vector<uint8_t> ImageCodec::encode_memory(const ImageBuffer& image, const EncodeOptions& options) {
    if (image.empty()) return {};

    if (options.format == ImageFormat::AVIF) {
        return AvifCodec::encode_memory(image, options);
    } else if (options.format == ImageFormat::JPEG) {
        return JpegCodec::encode_memory(image, options);
    }
    return JpegCodec::encode_memory(image, options);
}

bool ImageCodec::encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, const EncodeOptions& options) {
    if (image.empty()) return false;

    spdlog::debug("ImageCodec::encode_file: encoding to '{}' (format={}, quality={})",
                  output_path.string(), options.format == ImageFormat::AVIF ? "AVIF" : "JPEG", options.quality);

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

ImageBuffer ImageCodec::extract_frame(const std::filesystem::path& file_path,
                                     uint32_t frame_index,
                                     const DecodeOptions& options) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) return {};

    return AvifCodec::extract_frame(file_bytes, frame_index, options);
}

uint32_t ImageCodec::get_frame_count(const std::filesystem::path& file_path) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) return 0;

    return AvifCodec::get_frame_count(file_bytes);
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
