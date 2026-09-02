#include "image_odb/avif_codec.h"
#include "image_odb/image_codec.h"
#include "image_odb/image_odb.h"
#include <avif/avif.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>

namespace image_odb::codec {

namespace {

static constexpr std::string_view signature = "image-odb";
static constexpr std::string_view APP_XMP_METADATA = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description
    rdf:about="image-odb is a database for images, and it also supports conversions to AVIF format."
    xmlns:tiff="http://ns.adobe.com/tiff/1.0/">
   <tiff:Software>image-odb )" IMAGE_ODB_VERSION_STRING R"(</tiff:Software>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>)";

bool write_file_bytes(const std::filesystem::path& path, const uint8_t* data, size_t size) {
    if (auto parent = path.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

avifImage* create_avif_image_from_buffer(const ImageBuffer& img, const EncodeOptions& options) {
    if (img.empty() || img.width == 0 || img.height == 0) return nullptr;

    avifPixelFormat yuv_format = AVIF_PIXEL_FORMAT_YUV420;
    switch (options.subsampling) {
        case ChromaSubsampling::YUV420: yuv_format = AVIF_PIXEL_FORMAT_YUV420; break;
        case ChromaSubsampling::YUV422: yuv_format = AVIF_PIXEL_FORMAT_YUV422; break;
        case ChromaSubsampling::YUV444: yuv_format = AVIF_PIXEL_FORMAT_YUV444; break;
        case ChromaSubsampling::YUV400: yuv_format = AVIF_PIXEL_FORMAT_YUV400; break;
    }

    uint32_t depth = options.bit_depth;
    if (depth != 8 && depth != 10 && depth != 12) {
        depth = (img.bit_depth() > 8) ? 10 : 8;
    }

    avifImage* avif = avifImageCreate(img.width, img.height, depth, yuv_format);
    if (!avif) return nullptr;

    avifColorPrimaries cp = AVIF_COLOR_PRIMARIES_BT709;
    switch (img.color_profile.primaries) {
        case ColorPrimaries::BT709_sRGB: cp = AVIF_COLOR_PRIMARIES_BT709; break;
        case ColorPrimaries::BT2020: cp = AVIF_COLOR_PRIMARIES_BT2020; break;
        case ColorPrimaries::DCI_P3: cp = AVIF_COLOR_PRIMARIES_SMPTE432; break;
        case ColorPrimaries::Unspecified: cp = AVIF_COLOR_PRIMARIES_UNSPECIFIED; break;
    }

    avifTransferCharacteristics tc = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    switch (img.color_profile.transfer) {
        case TransferCharacteristics::sRGB: tc = AVIF_TRANSFER_CHARACTERISTICS_SRGB; break;
        case TransferCharacteristics::Linear: tc = AVIF_TRANSFER_CHARACTERISTICS_LINEAR; break;
        case TransferCharacteristics::PQ: tc = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084; break;
        case TransferCharacteristics::HLG: tc = AVIF_TRANSFER_CHARACTERISTICS_HLG; break;
    }

    avifMatrixCoefficients mc = AVIF_MATRIX_COEFFICIENTS_BT709;
    if (yuv_format == AVIF_PIXEL_FORMAT_YUV400) {
        mc = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    } else if (img.color_profile.primaries == ColorPrimaries::BT2020) {
        mc = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;
    }

    avif->matrixCoefficients = mc;
    avif->colorPrimaries = cp;
    avif->transferCharacteristics = tc;
    avif->yuvRange = AVIF_RANGE_FULL;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avif);
    rgb.format = (img.channels == 4 || 
                  img.format == PixelFormat::RGBA8 || 
                  img.format == PixelFormat::RGBA16 || 
                  img.format == PixelFormat::RGBA_F16)
                 ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.depth = (img.bit_depth() > 8) ? 16 : 8;
    rgb.pixels = const_cast<uint8_t*>(img.data.data());
    rgb.rowBytes = img.width * img.channels * (rgb.depth / 8);

    /**< XMP Metadata */
    avifImageSetMetadataXMP(
        avif, 
        reinterpret_cast<const uint8_t*>(APP_XMP_METADATA.data()), 
        APP_XMP_METADATA.size()
    );

    avifResult res = avifImageRGBToYUV(avif, &rgb);
    if (res != AVIF_RESULT_OK) {
        spdlog::error("avifImageRGBToYUV failed: {}", avifResultToString(res));
        avifImageDestroy(avif);
        return nullptr;
    }

    return avif;
}

} // namespace

bool AvifCodec::encode_still_image(const ImageBuffer& image,
                                   const std::filesystem::path& output_path,
                                   const EncodeOptions& options) {
    if (options.embed_thumbnail && !image.empty()) {
        auto thumb_buf = ImageCodec::resize_aspect_fit(image, options.thumbnail_dimension, options.thumbnail_dimension);
        if (!thumb_buf.empty()) {
            spdlog::debug("AvifCodec::encode_still_image: embedding thumbnail ({}x{}) into AVIF container",
                          thumb_buf.width, thumb_buf.height);
        }
    }

    avifImage* avif = create_avif_image_from_buffer(image, options);
    if (!avif) return false;

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder) {
        avifImageDestroy(avif);
        return false;
    }

    if (options.lossless) {
        encoder->quality = AVIF_QUALITY_LOSSLESS;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    } else {
        encoder->quality = std::clamp(options.quality, 1, 100);
        encoder->qualityAlpha = std::clamp(options.quality, 1, 100);
    }
    encoder->speed = std::clamp(options.speed, 0, 10);

    avifRWData raw = AVIF_DATA_EMPTY;
    avifResult res = avifEncoderWrite(encoder, avif, &raw);

    bool success = false;
    if (res == AVIF_RESULT_OK) {
        success = write_file_bytes(output_path, raw.data, raw.size);
    } else {
        spdlog::error("avifEncoderWrite failed: {}", avifResultToString(res));
    }

    avifRWDataFree(&raw);
    avifEncoderDestroy(encoder);
    avifImageDestroy(avif);

    return success;
}

bool AvifCodec::encode_still_image(const ImageBuffer& image,
                                   const std::filesystem::path& output_path,
                                   int quality, int speed) {
    EncodeOptions options;
    options.quality = quality;
    options.speed = speed;
    return encode_still_image(image, output_path, options);
}

bool AvifCodec::encode_burst_sequence(const std::vector<ImageBuffer>& frames,
                                      const std::filesystem::path& output_path,
                                      const EncodeOptions& options) {
    if (frames.empty()) return false;
    if (frames.size() == 1) {
        return encode_still_image(frames[0], output_path, options);
    }

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder) return false;

    if (options.lossless) {
        encoder->quality = AVIF_QUALITY_LOSSLESS;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    } else {
        encoder->quality = std::clamp(options.quality, 1, 100);
        encoder->qualityAlpha = std::clamp(options.quality, 1, 100);
    }
    encoder->speed = std::clamp(options.speed, 0, 10);
    encoder->timescale = 1; // 1 frame per duration unit

    bool all_ok = true;
    for (size_t i = 0; i < frames.size(); ++i) {
        avifImage* avif = create_avif_image_from_buffer(frames[i], options);
        if (!avif) {
            all_ok = false;
            break;
        }

        // Frame 0 is encoded as Keyframe (I-Frame), subsequent frames as Inter-frames (P-Frames)
        uint32_t flags = (i == 0) ? AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME : AVIF_ADD_IMAGE_FLAG_NONE;
        avifResult res = avifEncoderAddImage(encoder, avif, 1, flags);
        avifImageDestroy(avif);

        if (res != AVIF_RESULT_OK) {
            spdlog::error("avifEncoderAddImage failed at frame {}: {}", i, avifResultToString(res));
            all_ok = false;
            break;
        }
    }

    bool success = false;
    if (all_ok) {
        avifRWData raw = AVIF_DATA_EMPTY;
        avifResult res = avifEncoderFinish(encoder, &raw);
        if (res == AVIF_RESULT_OK) {
            success = write_file_bytes(output_path, raw.data, raw.size);
        } else {
            spdlog::error("avifEncoderFinish failed: {}", avifResultToString(res));
        }
        avifRWDataFree(&raw);
    }

    avifEncoderDestroy(encoder);
    return success;
}

bool AvifCodec::encode_burst_sequence(const std::vector<ImageBuffer>& frames,
                                      const std::filesystem::path& output_path,
                                      int quality, int speed) {
    EncodeOptions options;
    options.quality = quality;
    options.speed = speed;
    return encode_burst_sequence(frames, output_path, options);
}

bool AvifCodec::encode_burst_sequence(const std::vector<std::filesystem::path>& input_files,
                                      const std::filesystem::path& output_path,
                                      const EncodeOptions& options) {
    if (input_files.empty()) return false;

    std::vector<ImageBuffer> frames;
    frames.reserve(input_files.size());

    for (const auto& path : input_files) {
        auto img = ImageCodec::decode_file(path);
        if (img.empty()) {
            spdlog::error("Failed to decode burst frame input file: {}", path.string());
            return false;
        }
        frames.push_back(std::move(img));
    }

    return encode_burst_sequence(frames, output_path, options);
}

bool AvifCodec::encode_burst_sequence(const std::vector<std::filesystem::path>& input_files,
                                      const std::filesystem::path& output_path,
                                      int quality, int speed) {
    EncodeOptions options;
    options.quality = quality;
    options.speed = speed;
    return encode_burst_sequence(input_files, output_path, options);
}

ImageBuffer AvifCodec::extract_frame(const std::filesystem::path& avif_path,
                                     uint32_t frame_index,
                                     const DecodeOptions& options) {
    ImageBuffer buffer;
    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) return buffer;

    avifResult result = avifDecoderSetIOFile(decoder, avif_path.string().c_str());
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return buffer;
    }

    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return buffer;
    }

#ifdef IMAGE_SEQUENTAL_DETECT
    bool is_single_image = (decoder->imageCount == 1);
    bool is_sequence = (decoder->imageSequenceTrackPresent == AVIF_TRUE);
    if (is_single_image && !is_sequence) {
        // only one image is still_image
    } else {
        // multi-frame / burst sequence / animation
    }
#endif

    /**< is_encoded_by_image_odb */
    bool matched = false;
    if (decoder->image && decoder->image->xmp.size > 0 && decoder->image->xmp.data != nullptr) {
        std::string_view xmp_str(
            reinterpret_cast<const char*>(decoder->image->xmp.data),
            decoder->image->xmp.size
        );
        // check signature
        if (xmp_str.find(signature) != std::string_view::npos) {
            matched = true;
        }
    }

    if (frame_index >= static_cast<uint32_t>(decoder->imageCount)) {
        spdlog::error("Requested frame index {} exceeds total image count {}", frame_index, decoder->imageCount);
        avifDecoderDestroy(decoder);
        return buffer;
    }

    result = avifDecoderNthImage(decoder, frame_index);
    if (result != AVIF_RESULT_OK) {
        spdlog::error("avifDecoderNthImage failed: {}", avifResultToString(result));
        avifDecoderDestroy(decoder);
        return buffer;
    }

    bool want_alpha = (decoder->image->alphaPlane != nullptr) ||
                      (options.target_format == PixelFormat::RGBA8 || options.target_format == PixelFormat::RGBA16 || options.target_format == PixelFormat::RGBA_F16);

    bool is_16bit = (options.target_format == PixelFormat::RGB16 || options.target_format == PixelFormat::RGBA16 || decoder->image->depth > 8);

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = want_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.depth = is_16bit ? 16 : 8;
    if (avifRGBImageAllocatePixels(&rgb) != AVIF_RESULT_OK) {
        spdlog::error("avifRGBImageAllocate failed, but countinue anyway");
    }

    result = avifImageYUVToRGB(decoder->image, &rgb);
    if (result == AVIF_RESULT_OK) {
        buffer.width = rgb.width;
        buffer.height = rgb.height;
        buffer.channels = want_alpha ? 4 : 3;
        if (is_16bit) {
            buffer.format = want_alpha ? PixelFormat::RGBA16 : PixelFormat::RGB16;
        } else {
            buffer.format = want_alpha ? PixelFormat::RGBA8 : PixelFormat::RGB8;
        }

        // Extract color metadata
        switch (decoder->image->colorPrimaries) {
            case AVIF_COLOR_PRIMARIES_BT709: buffer.color_profile.primaries = ColorPrimaries::BT709_sRGB; break;
            case AVIF_COLOR_PRIMARIES_BT2020: buffer.color_profile.primaries = ColorPrimaries::BT2020; break;
            case AVIF_COLOR_PRIMARIES_SMPTE432:
            case AVIF_COLOR_PRIMARIES_SMPTE431: buffer.color_profile.primaries = ColorPrimaries::DCI_P3; break;
            default: buffer.color_profile.primaries = ColorPrimaries::Unspecified; break;
        }

        switch (decoder->image->transferCharacteristics) {
            case AVIF_TRANSFER_CHARACTERISTICS_SRGB: buffer.color_profile.transfer = TransferCharacteristics::sRGB; break;
            case AVIF_TRANSFER_CHARACTERISTICS_LINEAR: buffer.color_profile.transfer = TransferCharacteristics::Linear; break;
            case AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084: buffer.color_profile.transfer = TransferCharacteristics::PQ; break;
            case AVIF_TRANSFER_CHARACTERISTICS_HLG: buffer.color_profile.transfer = TransferCharacteristics::HLG; break;
            default: buffer.color_profile.transfer = TransferCharacteristics::sRGB; break;
        }

        size_t byte_count = static_cast<size_t>(rgb.width) * rgb.height * buffer.channels * (rgb.depth / 8);
        buffer.data.assign(rgb.pixels, rgb.pixels + byte_count);
    } else {
        spdlog::error("avifImageYUVToRGB failed: {}", avifResultToString(result));
    }

    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return buffer;
}

uint32_t AvifCodec::get_frame_count(const std::filesystem::path& avif_path) {
    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) return 0;

    if (avifDecoderSetIOFile(decoder, avif_path.string().c_str()) != AVIF_RESULT_OK ||
        avifDecoderParse(decoder) != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return 0;
    }

    uint32_t count = static_cast<uint32_t>(decoder->imageCount);
    avifDecoderDestroy(decoder);
    return count;
}

} // namespace image_odb::codec

