#include "image_odb/tiff_codec.h"
#include "image_odb/image_codec.h"
#include <tiffio.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <memory>

namespace image_odb::codec {

namespace {

struct MemTiffStream {
    const uint8_t* data{nullptr};
    tmsize_t size{0};
    tmsize_t offset{0};
};

tmsize_t tiff_mem_read(thandle_t handle, void* buf, tmsize_t size) {
    auto* s = static_cast<MemTiffStream*>(handle);
    if (!s || s->offset >= s->size || size <= 0) return 0;
    tmsize_t available = s->size - s->offset;
    tmsize_t to_read = std::min(size, available);
    std::memcpy(buf, s->data + s->offset, static_cast<size_t>(to_read));
    s->offset += to_read;
    return to_read;
}

tmsize_t tiff_mem_write(thandle_t, void*, tmsize_t) {
    return 0; // Read-only
}

toff_t tiff_mem_seek(thandle_t handle, toff_t offset, int whence) {
    auto* s = static_cast<MemTiffStream*>(handle);
    if (!s) return static_cast<toff_t>(-1);
    toff_t next_off = 0;
    switch (whence) {
        case SEEK_SET:
            next_off = offset;
            break;
        case SEEK_CUR:
            next_off = static_cast<toff_t>(s->offset) + offset;
            break;
        case SEEK_END:
            next_off = static_cast<toff_t>(s->size) + offset;
            break;
        default:
            return static_cast<toff_t>(-1);
    }
    if (next_off < 0 || next_off > static_cast<toff_t>(s->size)) {
        return static_cast<toff_t>(-1);
    }
    s->offset = static_cast<tmsize_t>(next_off);
    return next_off;
}

int tiff_mem_close(thandle_t) {
    return 0;
}

toff_t tiff_mem_size(thandle_t handle) {
    auto* s = static_cast<MemTiffStream*>(handle);
    return s ? static_cast<toff_t>(s->size) : 0;
}

int tiff_mem_map(thandle_t handle, void** base, toff_t* size) {
    auto* s = static_cast<MemTiffStream*>(handle);
    if (!s) return 0;
    *base = const_cast<void*>(static_cast<const void*>(s->data));
    *size = static_cast<toff_t>(s->size);
    return 1;
}

void tiff_mem_unmap(thandle_t, void*, toff_t) {
}

void tiff_dummy_error_handler(const char* module, const char* fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    spdlog::warn("LibTIFF error: [{}] {}", module ? module : "", buf);
}

void tiff_dummy_warning_handler(const char* module, const char* fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    spdlog::warn("LibTIFF warn: [{}] {}", module ? module : "", buf);
}

} // namespace

ImageBuffer TiffCodec::decode_memory(std::span<const uint8_t> data, const DecodeOptions& options) {
    if (data.empty()) return {};

    // Suppress default libtiff modal dialogs and stderr output
    TIFFSetErrorHandler(tiff_dummy_error_handler);
    TIFFSetWarningHandler(tiff_dummy_warning_handler);

    MemTiffStream stream{
        .data = data.data(),
        .size = static_cast<tmsize_t>(data.size()),
        .offset = 0
    };

    TIFF* tif = TIFFClientOpen("MemTIFF", "rm",
                               static_cast<thandle_t>(&stream),
                               tiff_mem_read, tiff_mem_write,
                               tiff_mem_seek, tiff_mem_close,
                               tiff_mem_size, tiff_mem_map,
                               tiff_mem_unmap);
    if (!tif) {
        spdlog::warn("TiffCodec: Failed to open TIFF from memory");
        return {};
    }

    std::unique_ptr<TIFF, decltype(&TIFFClose)> tif_guard(tif, TIFFClose);

    uint32_t width = 0;
    uint32_t height = 0;
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) ||
        width == 0 || height == 0) {
        spdlog::warn("TiffCodec: Failed to get valid TIFF dimensions");
        return {};
    }

    std::vector<uint32_t> raster(static_cast<size_t>(width) * height);
    if (!TIFFReadRGBAImageOriented(tif, width, height, raster.data(), ORIENTATION_TOPLEFT, 0)) {
        spdlog::warn("TiffCodec: Failed to read TIFF RGBA raster");
        return {};
    }

    // Determine target format
    PixelFormat target_format = PixelFormat::RGB8;
    uint32_t channels = 3;
    bool convert_to_gray = false;

    if (options.target_format.has_value()) {
        switch (*options.target_format) {
            case PixelFormat::GRAY8:
                target_format = PixelFormat::GRAY8;
                channels = 1;
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
        // Inspect if any pixel has non-opaque alpha
        bool has_alpha = false;
        for (uint32_t pixel : raster) {
            if (TIFFGetA(pixel) < 255) {
                has_alpha = true;
                break;
            }
        }
        if (has_alpha) {
            target_format = PixelFormat::RGBA8;
            channels = 4;
        } else {
            target_format = PixelFormat::RGB8;
            channels = 3;
        }
    }

    ImageBuffer buffer;
    buffer.width = width;
    buffer.height = height;
    buffer.channels = channels;
    buffer.format = target_format;
    buffer.color_profile.primaries = ColorPrimaries::BT709_sRGB;
    buffer.color_profile.transfer = TransferCharacteristics::sRGB;

    size_t pixel_count = static_cast<size_t>(width) * height;
    buffer.data.resize(pixel_count * channels);

    if (convert_to_gray) {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint32_t px = raster[i];
            uint32_t r = TIFFGetR(px);
            uint32_t g = TIFFGetG(px);
            uint32_t b = TIFFGetB(px);
            buffer.data[i] = static_cast<uint8_t>((299u * r + 587u * g + 114u * b + 500u) / 1000u);
        }
    } else if (channels == 4) {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint32_t px = raster[i];
            buffer.data[i * 4 + 0] = static_cast<uint8_t>(TIFFGetR(px));
            buffer.data[i * 4 + 1] = static_cast<uint8_t>(TIFFGetG(px));
            buffer.data[i * 4 + 2] = static_cast<uint8_t>(TIFFGetB(px));
            buffer.data[i * 4 + 3] = static_cast<uint8_t>(TIFFGetA(px));
        }
    } else {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint32_t px = raster[i];
            buffer.data[i * 3 + 0] = static_cast<uint8_t>(TIFFGetR(px));
            buffer.data[i * 3 + 1] = static_cast<uint8_t>(TIFFGetG(px));
            buffer.data[i * 3 + 2] = static_cast<uint8_t>(TIFFGetB(px));
        }
    }

    // Attach raw TIFF data as EXIF metadata payload so ExifReader can extract TIFF tags
    buffer.exif_data.assign(data.begin(), data.end());

    // Downscale if requested
    if (options.downscale_factor > 1) {
        uint32_t target_w = std::max(1u, buffer.width / options.downscale_factor);
        uint32_t target_h = std::max(1u, buffer.height / options.downscale_factor);
        buffer = ImageCodec::resize_aspect_fit(buffer, target_w, target_h);
    }

    return buffer;
}

} // namespace image_odb::codec
