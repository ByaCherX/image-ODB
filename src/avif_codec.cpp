#include "image_odb/avif_codec.h"
#include "image_odb/image_odb.h"
#include <avif/avif.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <thread>

namespace image_odb::codec {

namespace {

static constexpr std::string_view APP_XMP_METADATA = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description
    rdf:about="image-odb is a database for images, and it also supports conversions to AVIF format."
    xmlns:tiff="http://ns.adobe.com/tiff/1.0/">
   <tiff:Software>image-odb )" IMAGE_ODB_VERSION R"(</tiff:Software>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>)";

avifImage* avifFromBuffer(const ImageBuffer& img, const EncodeOptions& options) {
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
    avifResult res = avifImageSetMetadataXMP(
        avif, 
        reinterpret_cast<const uint8_t*>(APP_XMP_METADATA.data()), 
        APP_XMP_METADATA.size()
    );
    if (res != AVIF_RESULT_OK) {
        spdlog::warn("avifImageMetadataXMP failed: {}", avifResultToString(res));
    }

    res = avifImageRGBToYUV(avif, &rgb);
    if (res != AVIF_RESULT_OK) {
        spdlog::error("avifImageRGBToYUV failed: {}", avifResultToString(res));
        avifImageDestroy(avif);
        return nullptr;
    }

    // Attach EXIF metadata if present in ImageBuffer
    if (!img.exif_data.empty()) {
        avifResult exif_res = avifImageSetMetadataExif(avif, img.exif_data.data(), img.exif_data.size());
        if (exif_res != AVIF_RESULT_OK) {
            spdlog::warn("avifImageSetMetadataExif notice: {}", avifResultToString(exif_res));
        }
    }

    return avif;
}

int resolve_encoder_threads(int requested_threads) {
    if (requested_threads > 0) {
        return requested_threads;
    }
    unsigned int hw = std::thread::hardware_concurrency();
    return (hw > 0) ? static_cast<int>(hw) : 1;
}

} // namespace

std::vector<uint8_t> AvifCodec::encode_memory(const ImageBuffer& image, const EncodeOptions& options) {
    if (image.empty()) return {};

    avifImage* avif = avifFromBuffer(image, options);
    if (!avif) return {};

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder) {
        avifImageDestroy(avif);
        return {};
    }

    if (options.lossless) {
        encoder->quality = AVIF_QUALITY_LOSSLESS;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    } else {
        encoder->quality = std::clamp(options.quality, 1, 100);
        encoder->qualityAlpha = std::clamp(options.quality, 1, 100);
    }
    encoder->speed = std::clamp(options.speed, 0, 10);
    encoder->maxThreads = resolve_encoder_threads(options.threads);

    //---------------------------------------------------------------------------------------+
    // The encoder to be used for the AVIF image is determined here. AOM is used by default; |
    // use AOM for the most balanced and compatible encoding. While other encoders may work, |
    // they can result in corrupted image areas, requiring additional work and testing.      |
    //                                                                                       |
    // If you wish to select a different encoder, you will need to change this manually.     |
    //---------------------------------------------------------------------------------------+
    encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;

    avifRWData raw = AVIF_DATA_EMPTY;
    avifResult res = avifEncoderWrite(encoder, avif, &raw);

    std::vector<uint8_t> out;
    if (res == AVIF_RESULT_OK && raw.data && raw.size > 0) {
        out.assign(raw.data, raw.data + raw.size);
    } else {
        spdlog::error("avifEncoderWrite failed: {}", avifResultToString(res));
    }

    avifRWDataFree(&raw);
    avifEncoderDestroy(encoder);
    avifImageDestroy(avif);

    return out;
}

std::vector<uint8_t> AvifCodec::encode_burst_sequence(const std::vector<ImageBuffer>& frames,
                                                      const EncodeOptions& options) {
    if (frames.empty()) return {};
    if (frames.size() == 1) {
        return encode_memory(frames[0], options);
    }

    avifEncoder* encoder = avifEncoderCreate();
    if (!encoder) return {};

    if (options.lossless) {
        encoder->quality = AVIF_QUALITY_LOSSLESS;
        encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    } else {
        encoder->quality = std::clamp(options.quality, 1, 100);
        encoder->qualityAlpha = std::clamp(options.quality, 1, 100);
    }
    encoder->speed = std::clamp(options.speed, 0, 10);
    encoder->maxThreads = resolve_encoder_threads(options.threads);
    encoder->timescale = 1; // 1 frame per duration unit

    bool all_ok = true;
    for (size_t i = 0; i < frames.size(); ++i) {
        avifImage* avif = avifFromBuffer(frames[i], options);
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

    std::vector<uint8_t> out;
    if (all_ok) {
        avifRWData raw = AVIF_DATA_EMPTY;
        avifResult res = avifEncoderFinish(encoder, &raw);
        if (res == AVIF_RESULT_OK && raw.data && raw.size > 0) {
            out.assign(raw.data, raw.data + raw.size);
        } else {
            spdlog::error("avifEncoderFinish failed: {}", avifResultToString(res));
        }
        avifRWDataFree(&raw);
    }

    avifEncoderDestroy(encoder);
    return out;
}

ImageBuffer AvifCodec::decode_memory(std::span<const uint8_t> data, const DecodeOptions& options) {
    return extract_frame(data, 0, options);
}

ImageBuffer AvifCodec::extract_frame(std::span<const uint8_t> data,
                                     uint32_t frame_index,
                                     const DecodeOptions& options) {
    ImageBuffer buffer;
    if (data.empty()) return buffer;

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) return buffer;
    decoder->codecChoice = AVIF_CODEC_CHOICE_DAV1D;
    
/*helper*/
#define avifResultError(result, spdlog_message)                                \
    if ((result) != AVIF_RESULT_OK) {                                          \
        spdlog::error("{}: {}", (spdlog_message), avifResultToString(result)); \
        avifDecoderDestroy(decoder);                                           \
        return buffer;                                                         \
    }
    avifResult result;

    result = avifDecoderSetIOMemory(decoder, data.data(), data.size());
    avifResultError(result, "avifDecoderSetIOMemory failed");

    result = avifDecoderParse(decoder);
    avifResultError(result, "avifDecoderParse failed");

    if (frame_index >= static_cast<uint32_t>(decoder->imageCount)) {
        spdlog::error("Requested frame index {} exceeds total image count {}", frame_index, decoder->imageCount);
        avifDecoderDestroy(decoder);
        return buffer;
    }

    result = avifDecoderNthImage(decoder, frame_index);
    avifResultError(result, "avifDecoderNthImage failed");

    bool want_alpha = (decoder->image->alphaPlane != nullptr) ||
                      (options.target_format == PixelFormat::RGBA8 || options.target_format == PixelFormat::RGBA16 || options.target_format == PixelFormat::RGBA_F16);

    bool is_16bit = (options.target_format == PixelFormat::RGB16 || options.target_format == PixelFormat::RGBA16 || decoder->image->depth > 8);

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = want_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.depth = is_16bit ? 16 : 8;
    result = avifRGBImageAllocatePixels(&rgb);
    avifResultError(result, "avifRGBImageAllocatePixels failed");

    result = avifImageYUVToRGB(decoder->image, &rgb);
    if (result != AVIF_RESULT_OK) {
        avifRGBImageFreePixels(&rgb);
    }
    avifResultError(result, "avifImageYUVToRGB failed");

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

    // Extract EXIF metadata if present in AVIF container
    const avifRWData& exif = decoder->image->exif;
    if (exif.data != nullptr && exif.size > 0) {
        buffer.exif_data.assign(exif.data, exif.data + exif.size);
    }

    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return buffer;
#undef avifResultError /*helper*/
}

uint32_t AvifCodec::get_frame_count(std::span<const uint8_t> data) {
    if (data.empty()) return 0;

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) return 0;

    if (avifDecoderSetIOMemory(decoder, data.data(), data.size()) != AVIF_RESULT_OK ||
        avifDecoderParse(decoder) != AVIF_RESULT_OK) {
        avifDecoderDestroy(decoder);
        return 0;
    }

    uint32_t count = static_cast<uint32_t>(decoder->imageCount);
    avifDecoderDestroy(decoder);
    return count;
}

} // namespace image_odb::codec

