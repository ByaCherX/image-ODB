#include "image.h"
#include "image_odb/image_odb.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <filesystem>
#include <string>

namespace image_odb::cli {

namespace {

int handle_image(const std::string& input_file,
                 const std::string& output_file,
                 bool encode_mode,
                 bool decode_mode,
                 int quality,
                 int speed,
                 bool lossless,
                 const std::string& subsampling_str,
                 int bit_depth,
                 bool embed_thumb) {
    if (!std::filesystem::exists(input_file)) {
        std::cerr << "Error: Input file does not exist: " << input_file << "\n";
        return 1;
    }

    std::filesystem::path in_p(input_file);
    std::filesystem::path out_p = output_file;

    // Determine target mode and output filename if unspecified
    if (!encode_mode && !decode_mode) {
        auto in_fmt = codec::ImageCodec::detect_format(in_p);
        if (in_fmt == ImageFormat::AVIF) {
            decode_mode = true;
        } else {
            encode_mode = true;
        }
    }

    if (decode_mode) {
        if (out_p.empty()) {
            out_p = in_p.parent_path() / (in_p.stem().string() + ".jpg");
        }

        spdlog::debug("Decoding AVIF '{}' -> '{}'", input_file, out_p.string());
        auto img = codec::ImageCodec::extract_frame(in_p, 0);
        if (img.empty()) {
            std::cerr << "Error: Failed to decode input AVIF file: " << input_file << "\n";
            return 1;
        }

        EncodeOptions enc_opts;
        enc_opts.format = ImageFormat::JPEG;
        enc_opts.quality = (quality > 0) ? quality : 85;

        if (codec::ImageCodec::encode_file(img, out_p, enc_opts)) {
            std::cout << "Successfully decoded '" << input_file << "' -> '" << out_p.string() << "' (" 
                      << img.width << "x" << img.height << ")\n";
            return 0;
        }
        std::cerr << "Error: Failed to encode decoded image to: " << out_p.string() << "\n";
        return 1;
    } else {
        if (out_p.empty()) {
            out_p = in_p.parent_path() / (in_p.stem().string() + ".avif");
        }

        spdlog::debug("Encoding image '{}' -> AVIF '{}'", input_file, out_p.string());
        auto img = codec::ImageCodec::decode_file(in_p);
        if (img.empty()) {
            std::cerr << "Error: Failed to decode input image: " << input_file << "\n";
            return 1;
        }

        EncodeOptions enc_opts;
        enc_opts.format = ImageFormat::AVIF;
        enc_opts.quality = (quality > 0) ? quality : 80;
        enc_opts.speed = speed;
        enc_opts.lossless = lossless;
        enc_opts.bit_depth = bit_depth;
        enc_opts.embed_thumbnail = embed_thumb;

        if (subsampling_str == "444") enc_opts.subsampling = ChromaSubsampling::YUV444;
        else if (subsampling_str == "422") enc_opts.subsampling = ChromaSubsampling::YUV422;
        else if (subsampling_str == "400") enc_opts.subsampling = ChromaSubsampling::YUV400;
        else enc_opts.subsampling = ChromaSubsampling::YUV420;

        if (codec::ImageCodec::encode_file(img, out_p, enc_opts)) {
            std::cout << "Successfully encoded '" << input_file << "' -> '" << out_p.string() << "' (AVIF, " 
                      << img.width << "x" << img.height << ", quality=" << enc_opts.quality << ")\n";
            return 0;
        }
        std::cerr << "Error: Failed to encode image to AVIF: " << out_p.string() << "\n";
        return 1;
    }
}

} // namespace

void register_image_command(CLI::App& app) {
    auto* image_cmd = app.add_subcommand("image", "Encode any image format to AVIF or decode AVIF to JPEG");
    image_cmd->fallthrough();

    static std::string img_in;
    static std::string img_out = "";
    static bool img_encode = false;
    static bool img_decode = false;
    static int img_quality = 80;
    static int img_speed = 6;
    static bool img_lossless = false;
    static std::string img_subsampling = "420";
    static int img_depth = 8;
    static bool img_embed_thumb = false;

    image_cmd->add_option("input", img_in, "Input image file path")->required();
    image_cmd->add_option("-o,--output", img_out, "Output destination image path");
    image_cmd->add_flag("-e,--encode", img_encode, "Encode input image to AVIF");
    image_cmd->add_flag("-d,--decode", img_decode, "Decode AVIF input image to JPEG");
    image_cmd->add_option("-q,--quality", img_quality, "Compression quality (1-100)")->default_val(80);
    image_cmd->add_option("-s,--speed", img_speed, "AVIF encoder CPU speed (0-10)")->default_val(6);
    image_cmd->add_flag("--lossless", img_lossless, "Enable lossless encoding");
    image_cmd->add_option("--subsampling", img_subsampling, "Chroma subsampling (420, 422, 444, 400)")->default_val("420");
    image_cmd->add_option("--depth,--bit-depth", img_depth, "Bit depth (8, 10, 12)")->default_val(8);
    image_cmd->add_flag("--embed-thumb", img_embed_thumb, "Embed downscaled thumbnail inside AVIF");

    image_cmd->callback([]() {
        return handle_image(img_in, img_out, img_encode, img_decode, img_quality, img_speed, img_lossless, img_subsampling, img_depth, img_embed_thumb);
    });
}

} // namespace image_odb::cli
