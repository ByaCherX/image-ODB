#include "image_odb/jpeg_codec.h"
#include <spdlog/spdlog.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <fstream>
#include <cstdlib>
#include <algorithm>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4611 4324)
#endif

namespace image_odb::codec {

namespace {

struct CustomJpegErrorMgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

struct RawDecompressResult {
    uint8_t* data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
};

struct RawCompressResult {
    unsigned char* data = nullptr;
    unsigned long size = 0;
};

void custom_jpeg_error_exit(j_common_ptr cinfo) {
    auto* myerr = reinterpret_cast<CustomJpegErrorMgr*>(cinfo->err);
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    spdlog::warn("libjpeg error: {}", buffer);
    longjmp(myerr->setjmp_buffer, 1);
}

void custom_jpeg_output_message(j_common_ptr cinfo) {
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    spdlog::debug("libjpeg message: {}", buffer);
}

std::vector<uint8_t> read_file_bytes(const std::filesystem::path& file_path) {
    std::ifstream infile(file_path, std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        spdlog::warn("Cannot open JPEG file for reading: {}", file_path.string());
        return {};
    }

    auto file_size = infile.tellg();
    if (file_size <= 0) {
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    infile.seekg(0, std::ios::beg);
    infile.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!infile.good() && !infile.eof()) {
        spdlog::warn("Failed to read all bytes from JPEG file: {}", file_path.string());
        return {};
    }

    return buffer;
}

void apply_jpeg_subsampling(jpeg_compress_struct* cinfo, ChromaSubsampling subsampling) {
    if (cinfo->in_color_space != JCS_RGB) return;
    if (subsampling == ChromaSubsampling::YUV444) {
        cinfo->comp_info[0].h_samp_factor = 1;
        cinfo->comp_info[0].v_samp_factor = 1;
        cinfo->comp_info[1].h_samp_factor = 1;
        cinfo->comp_info[1].v_samp_factor = 1;
        cinfo->comp_info[2].h_samp_factor = 1;
        cinfo->comp_info[2].v_samp_factor = 1;
    } else if (subsampling == ChromaSubsampling::YUV422) {
        cinfo->comp_info[0].h_samp_factor = 2;
        cinfo->comp_info[0].v_samp_factor = 1;
        cinfo->comp_info[1].h_samp_factor = 1;
        cinfo->comp_info[1].v_samp_factor = 1;
        cinfo->comp_info[2].h_samp_factor = 1;
        cinfo->comp_info[2].v_samp_factor = 1;
    } else { // YUV420 standard
        cinfo->comp_info[0].h_samp_factor = 2;
        cinfo->comp_info[0].v_samp_factor = 2;
        cinfo->comp_info[1].h_samp_factor = 1;
        cinfo->comp_info[1].v_samp_factor = 1;
        cinfo->comp_info[2].h_samp_factor = 1;
        cinfo->comp_info[2].v_samp_factor = 1;
    }
}

RawDecompressResult raw_decompress_jpeg(const uint8_t* data, size_t size, int downscale_factor, bool is_grayscale) {
    RawDecompressResult res;
    struct jpeg_decompress_struct cinfo;
    struct CustomJpegErrorMgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = custom_jpeg_error_exit;
    jerr.pub.output_message = custom_jpeg_output_message;

    if (setjmp(jerr.setjmp_buffer)) {
        if (res.data) {
            std::free(res.data);
            res.data = nullptr;
        }
        jpeg_destroy_decompress(&cinfo);
        return {};
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, const_cast<unsigned char*>(data), static_cast<unsigned long>(size));
    jpeg_read_header(&cinfo, TRUE);

    if (downscale_factor >= 8) {
        cinfo.scale_num = 1;
        cinfo.scale_denom = 8;
    } else if (downscale_factor >= 4) {
        cinfo.scale_num = 1;
        cinfo.scale_denom = 4;
    } else if (downscale_factor >= 2) {
        cinfo.scale_num = 1;
        cinfo.scale_denom = 2;
    }

    cinfo.out_color_space = is_grayscale ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_start_decompress(&cinfo);

    res.width = cinfo.output_width;
    res.height = cinfo.output_height;
    res.channels = (cinfo.out_color_space == JCS_GRAYSCALE) ? 1 : 3;
    size_t total_bytes = static_cast<size_t>(res.width) * res.height * res.channels;

    res.data = static_cast<uint8_t*>(std::malloc(total_bytes));
    if (!res.data) {
        jpeg_destroy_decompress(&cinfo);
        return {};
    }

    int row_stride = res.width * res.channels;
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* row_pointer = res.data + (cinfo.output_scanline * row_stride);
        jpeg_read_scanlines(&cinfo, &row_pointer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return res;
}

RawCompressResult raw_compress_jpeg(const uint8_t* img_data, uint32_t width, uint32_t height, 
                                    uint32_t channels, int quality, ChromaSubsampling subsampling) {
    struct jpeg_compress_struct cinfo;
    struct CustomJpegErrorMgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = custom_jpeg_error_exit;
    jerr.pub.output_message = custom_jpeg_output_message;

    unsigned char* mem_buf = nullptr;
    unsigned long mem_size = 0;
    uint8_t* row_buf = static_cast<uint8_t*>(std::malloc(width * 3));

    if (setjmp(jerr.setjmp_buffer)) {
        if (row_buf) std::free(row_buf);
        if (mem_buf) std::free(mem_buf);
        jpeg_destroy_compress(&cinfo);
        return {};
    }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &mem_buf, &mem_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    apply_jpeg_subsampling(&cinfo, subsampling);
    jpeg_set_quality(&cinfo, std::clamp(quality, 1, 100), TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        const uint8_t* row_src = img_data + (cinfo.next_scanline * width * channels);
        if (channels == 3) {
            JSAMPROW row_pointer = const_cast<JSAMPROW>(row_src);
            jpeg_write_scanlines(&cinfo, &row_pointer, 1);
        } else if (channels == 4) {
            for (uint32_t x = 0; x < width; ++x) {
                row_buf[x * 3 + 0] = row_src[x * 4 + 0];
                row_buf[x * 3 + 1] = row_src[x * 4 + 1];
                row_buf[x * 3 + 2] = row_src[x * 4 + 2];
            }
            JSAMPROW row_pointer = row_buf;
            jpeg_write_scanlines(&cinfo, &row_pointer, 1);
        } else if (channels == 1) {
            for (uint32_t x = 0; x < width; ++x) {
                row_buf[x * 3 + 0] = row_src[x];
                row_buf[x * 3 + 1] = row_src[x];
                row_buf[x * 3 + 2] = row_src[x];
            }
            JSAMPROW row_pointer = row_buf;
            jpeg_write_scanlines(&cinfo, &row_pointer, 1);
        }
    }

    jpeg_finish_compress(&cinfo);
    if (row_buf) {
        std::free(row_buf);
        row_buf = nullptr;
    }

    jpeg_destroy_compress(&cinfo);
    return {mem_buf, mem_size};
}

} // namespace

ImageBuffer JpegCodec::decode_file(const std::filesystem::path& file_path, const DecodeOptions& options) {
    auto file_bytes = read_file_bytes(file_path);
    if (file_bytes.empty()) {
        return {};
    }
    return decode_memory(file_bytes, options);
}

ImageBuffer JpegCodec::decode_memory(std::span<const uint8_t> data, const DecodeOptions& options) {
    if (data.empty()) return {};

    bool is_gray = (options.target_format == PixelFormat::GRAY8);
    auto raw = raw_decompress_jpeg(data.data(), 
                                   data.size(), 
                                   options.downscale_factor, 
                                   is_gray);
    if (!raw.data) {
        return {};
    }

    ImageBuffer buffer;
    buffer.width = raw.width;
    buffer.height = raw.height;
    buffer.channels = raw.channels;
    buffer.format = (raw.channels == 1) ? PixelFormat::GRAY8 : PixelFormat::RGB8;
    buffer.color_profile.primaries = ColorPrimaries::BT709_sRGB;
    buffer.color_profile.transfer = TransferCharacteristics::sRGB;
    buffer.data.assign(raw.data, raw.data + (static_cast<size_t>(raw.width) * raw.height * raw.channels));

    std::free(raw.data);
    return buffer;
}

std::vector<uint8_t> JpegCodec::encode_memory(const ImageBuffer& image, const EncodeOptions& options) {
    if (image.empty()) return {};

    auto raw = raw_compress_jpeg(image.data.data(), 
                                 image.width, 
                                 image.height, 
                                 image.channels, 
                                 options.quality, 
                                 options.subsampling);
    if (!raw.data || raw.size == 0) return {};

    std::vector<uint8_t> result(raw.data, raw.data + raw.size);
    std::free(raw.data);
    return result;
}

std::vector<uint8_t> JpegCodec::encode_memory(const ImageBuffer& image, int quality) {
    EncodeOptions options;
    options.quality = quality;
    return encode_memory(image, options);
}

bool JpegCodec::encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, const EncodeOptions& options) {
    auto data = encode_memory(image, options);
    if (data.empty()) return false;

    if (auto parent = output_path.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open()) {
        spdlog::error("Cannot create JPEG output file: {}", output_path.string());
        return false;
    }

    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool JpegCodec::encode_file(const ImageBuffer& image, const std::filesystem::path& output_path, int quality) {
    EncodeOptions options;
    options.quality = quality;
    return encode_file(image, output_path, options);
}

} // namespace image_odb::codec

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
