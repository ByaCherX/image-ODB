#include "image_odb/thumbhash.h"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace image_odb::hash {

namespace {

const std::string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode(const uint8_t* buf, size_t buf_len) {
    std::string ret;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    while (buf_len--) {
        char_array_3[i++] = *(buf++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = static_cast<uint8_t>(((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4));
            char_array_4[2] = static_cast<uint8_t>(((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6));
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += BASE64_CHARS[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = static_cast<uint8_t>(((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4));
        char_array_4[2] = static_cast<uint8_t>(((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6));

        for (int j = 0; (j < i + 1); j++)
            ret += BASE64_CHARS[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

std::vector<uint8_t> base64_decode(const std::string& encoded_string) {
    size_t in_len = encoded_string.size();
    int i = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::vector<uint8_t> ret;

    while (in_len-- && (encoded_string[in_] != '=') &&
           (isalnum(encoded_string[in_]) || (encoded_string[in_] == '+') || (encoded_string[in_] == '/'))) {
        char_array_4[i++] = static_cast<uint8_t>(encoded_string[in_]); in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = static_cast<uint8_t>(BASE64_CHARS.find(static_cast<char>(char_array_4[i])));

            char_array_3[0] = static_cast<uint8_t>((char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4));
            char_array_3[1] = static_cast<uint8_t>(((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2));
            char_array_3[2] = static_cast<uint8_t>(((char_array_4[2] & 0x3) << 6) + char_array_4[3]);

            for (i = 0; (i < 3); i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (int j = 0; j < i; j++)
            char_array_4[j] = static_cast<uint8_t>(BASE64_CHARS.find(static_cast<char>(char_array_4[j])));

        char_array_3[0] = static_cast<uint8_t>((char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4));
        char_array_3[1] = static_cast<uint8_t>(((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2));

        for (int j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
    }

    return ret;
}

struct ChannelDCT {
    double dc{0.0};
    std::vector<double> ac;
    double scale{0.0};
    uint32_t nx{0};
    uint32_t ny{0};
};

ChannelDCT compute_channel_dct(const std::vector<double>& channel, uint32_t w, uint32_t h, uint32_t nx, uint32_t ny) {
    ChannelDCT result;
    result.nx = nx;
    result.ny = ny;
    std::vector<double> coefficients(nx * ny, 0.0);

    for (uint32_t v = 0; v < ny; ++v) {
        for (uint32_t u = 0; u < nx; ++u) {
            double sum = 0.0;
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    sum += channel[y * w + x] *
                           std::cos(((2 * x + 1) * u * M_PI) / (2.0 * w)) *
                           std::cos(((2 * y + 1) * v * M_PI) / (2.0 * h));
                }
            }
            double cu = (u == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
            double cv = (v == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
            coefficients[v * nx + u] = (2.0 / std::sqrt(static_cast<double>(w * h))) * cu * cv * sum;
        }
    }

    result.dc = coefficients[0];
    double max_ac = 0.0;
    for (size_t i = 1; i < coefficients.size(); ++i) {
        max_ac = std::max(max_ac, std::abs(coefficients[i]));
    }
    result.scale = max_ac;
    result.ac.assign(coefficients.begin() + 1, coefficients.end());
    return result;
}

} // namespace

std::vector<uint8_t> ThumbHash::encode(const ImageBuffer& image) {
    if (image.empty() || image.width == 0 || image.height == 0) {
        return {};
    }

    // Target sample size (<= 32 for fast ThumbHash DCT)
    uint32_t sample_w = std::clamp(image.width, 1u, 32u);
    uint32_t sample_h = std::clamp(image.height, 1u, 32u);
    if (image.width > image.height) {
        sample_w = 32;
        sample_h = std::max(1u, static_cast<uint32_t>(std::round(32.0 * image.height / image.width)));
    } else {
        sample_h = 32;
        sample_w = std::max(1u, static_cast<uint32_t>(std::round(32.0 * image.width / image.height)));
    }

    // Convert and resample to L, P, Q, A channels
    std::vector<double> L(sample_w * sample_h, 0.0);
    std::vector<double> P(sample_w * sample_h, 0.0);
    std::vector<double> Q(sample_w * sample_h, 0.0);
    std::vector<double> A(sample_w * sample_h, 1.0);
    bool has_alpha = false;

    for (uint32_t y = 0; y < sample_h; ++y) {
        uint32_t src_y = std::min(image.height - 1, static_cast<uint32_t>(y * image.height / sample_h));
        for (uint32_t x = 0; x < sample_w; ++x) {
            uint32_t src_x = std::min(image.width - 1, static_cast<uint32_t>(x * image.width / sample_w));
            size_t idx = (src_y * image.width + src_x) * image.channels;

            double r = image.data[idx] / 255.0;
            double g = (image.channels >= 2) ? (image.data[idx + 1] / 255.0) : r;
            double b = (image.channels >= 3) ? (image.data[idx + 2] / 255.0) : r;
            double a = (image.channels >= 4) ? (image.data[idx + 3] / 255.0) : 1.0;

            if (a < 1.0) has_alpha = true;

            size_t dst_idx = y * sample_w + x;
            L[dst_idx] = (r + g + b) / 3.0;
            P[dst_idx] = (r + g) / 2.0 - b;
            Q[dst_idx] = r - g;
            A[dst_idx] = a;
        }
    }

    // Determine grid size
    uint32_t lx = (sample_w > sample_h) ? 7 : std::max(1u, static_cast<uint32_t>(std::round(7.0 * sample_w / sample_h)));
    uint32_t ly = (sample_h >= sample_w) ? 7 : std::max(1u, static_cast<uint32_t>(std::round(7.0 * sample_h / sample_w)));
    lx = std::clamp(lx, 1u, 7u);
    ly = std::clamp(ly, 1u, 7u);

    auto dct_L = compute_channel_dct(L, sample_w, sample_h, lx, ly);
    auto dct_P = compute_channel_dct(P, sample_w, sample_h, 3, 3);
    auto dct_Q = compute_channel_dct(Q, sample_w, sample_h, 3, 3);

    // Pack into binary format (~25 bytes)
    std::vector<uint8_t> out;
    out.reserve(25);

    // Header 1: L_dc (6 bits), P_dc (6 bits), Q_dc (6 bits), L_scale (5 bits), has_alpha (1 bit) -> 24 bits (3 bytes)
    uint32_t l_dc = static_cast<uint32_t>(std::clamp(std::round(dct_L.dc * 63.0), 0.0, 63.0));
    uint32_t p_dc = static_cast<uint32_t>(std::clamp(std::round((dct_P.dc + 1.0) * 31.5), 0.0, 63.0));
    uint32_t q_dc = static_cast<uint32_t>(std::clamp(std::round((dct_Q.dc + 1.0) * 31.5), 0.0, 63.0));
    uint32_t l_scale = static_cast<uint32_t>(std::clamp(std::round(dct_L.scale * 31.0), 0.0, 31.0));

    uint32_t header24 = (l_dc << 18) | (p_dc << 12) | (q_dc << 6) | (l_scale << 1) | (has_alpha ? 1 : 0);
    out.push_back(static_cast<uint8_t>((header24 >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((header24 >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(header24 & 0xFF));

    // Header 2: is_landscape (1 bit), l_max (3 bits), p_scale (6 bits), q_scale (6 bits) -> 16 bits (2 bytes)
    bool is_landscape = (sample_w > sample_h);
    uint32_t l_dim = is_landscape ? ly : lx;
    uint32_t p_scale = static_cast<uint32_t>(std::clamp(std::round(dct_P.scale * 63.0), 0.0, 63.0));
    uint32_t q_scale = static_cast<uint32_t>(std::clamp(std::round(dct_Q.scale * 63.0), 0.0, 63.0));

    uint16_t header16 = static_cast<uint16_t>(((is_landscape ? 1 : 0) << 15) | ((l_dim & 7) << 12) | ((p_scale & 63) << 6) | (q_scale & 63));
    out.push_back(static_cast<uint8_t>((header16 >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(header16 & 0xFF));

    // Quantize and pack L AC coefficients (4 bits each, 2 per byte)
    for (size_t i = 0; i < dct_L.ac.size(); i += 2) {
        double val1 = (dct_L.scale > 0.0) ? (dct_L.ac[i] / dct_L.scale) : 0.0;
        uint8_t q1 = static_cast<uint8_t>(std::clamp(std::round((val1 + 1.0) * 7.5), 0.0, 15.0));

        uint8_t q2 = 0;
        if (i + 1 < dct_L.ac.size()) {
            double val2 = (dct_L.scale > 0.0) ? (dct_L.ac[i + 1] / dct_L.scale) : 0.0;
            q2 = static_cast<uint8_t>(std::clamp(std::round((val2 + 1.0) * 7.5), 0.0, 15.0));
        }
        out.push_back(static_cast<uint8_t>((q1 << 4) | q2));
    }

    // Quantize and pack P & Q AC coefficients (4 bits each)
    for (size_t i = 0; i < dct_P.ac.size(); i += 2) {
        double val1 = (dct_P.scale > 0.0) ? (dct_P.ac[i] / dct_P.scale) : 0.0;
        uint8_t q1 = static_cast<uint8_t>(std::clamp(std::round((val1 + 1.0) * 7.5), 0.0, 15.0));

        uint8_t q2 = 0;
        if (i + 1 < dct_P.ac.size()) {
            double val2 = (dct_P.scale > 0.0) ? (dct_P.ac[i + 1] / dct_P.scale) : 0.0;
            q2 = static_cast<uint8_t>(std::clamp(std::round((val2 + 1.0) * 7.5), 0.0, 15.0));
        }
        out.push_back(static_cast<uint8_t>((q1 << 4) | q2));
    }

    for (size_t i = 0; i < dct_Q.ac.size(); i += 2) {
        double val1 = (dct_Q.scale > 0.0) ? (dct_Q.ac[i] / dct_Q.scale) : 0.0;
        uint8_t q1 = static_cast<uint8_t>(std::clamp(std::round((val1 + 1.0) * 7.5), 0.0, 15.0));

        uint8_t q2 = 0;
        if (i + 1 < dct_Q.ac.size()) {
            double val2 = (dct_Q.scale > 0.0) ? (dct_Q.ac[i + 1] / dct_Q.scale) : 0.0;
            q2 = static_cast<uint8_t>(std::clamp(std::round((val2 + 1.0) * 7.5), 0.0, 15.0));
        }
        out.push_back(static_cast<uint8_t>((q1 << 4) | q2));
    }

    spdlog::debug("ThumbHash::encode: generated {} bytes hash for {}x{} image", out.size(), image.width, image.height);
    return out;
}

std::string ThumbHash::encode_to_base64(const ImageBuffer& image) {
    auto raw = encode(image);
    if (raw.empty()) return "";
    return base64_encode(raw.data(), raw.size());
}

ThumbHashInfo ThumbHash::extract_info(std::span<const uint8_t> hash_bytes) {
    ThumbHashInfo info;
    if (hash_bytes.size() < 5) return info;

    uint32_t header24 = (static_cast<uint32_t>(hash_bytes[0]) << 16) |
                        (static_cast<uint32_t>(hash_bytes[1]) << 8) |
                        static_cast<uint32_t>(hash_bytes[2]);
    info.has_alpha = (header24 & 1) != 0;

    uint16_t header16 = static_cast<uint16_t>((static_cast<uint16_t>(hash_bytes[3]) << 8) | static_cast<uint16_t>(hash_bytes[4]));
    bool is_landscape = ((header16 >> 15) & 1) != 0;
    uint32_t l_dim = std::max<uint32_t>(1u, (header16 >> 12) & 7);

    if (is_landscape) {
        info.approximate_aspect_ratio = 7.0f / static_cast<float>(l_dim);
    } else {
        info.approximate_aspect_ratio = static_cast<float>(l_dim) / 7.0f;
    }

    return info;
}

ImageBuffer ThumbHash::decode(std::span<const uint8_t> hash_bytes,
                              uint32_t target_width,
                              uint32_t target_height) {
    ImageBuffer result;
    if (hash_bytes.size() < 5) return result;

    // Unpack Header 1
    uint32_t header24 = (static_cast<uint32_t>(hash_bytes[0]) << 16) |
                        (static_cast<uint32_t>(hash_bytes[1]) << 8) |
                        static_cast<uint32_t>(hash_bytes[2]);
    double l_dc = ((header24 >> 18) & 63) / 63.0;
    double p_dc = (((header24 >> 12) & 63) / 31.5) - 1.0;
    double q_dc = (((header24 >> 6) & 63) / 31.5) - 1.0;
    double l_scale = ((header24 >> 1) & 31) / 31.0;
    bool has_alpha = (header24 & 1) != 0;

    // Unpack Header 2
    uint16_t header16 = static_cast<uint16_t>((static_cast<uint16_t>(hash_bytes[3]) << 8) | static_cast<uint16_t>(hash_bytes[4]));
    bool is_landscape = ((header16 >> 15) & 1) != 0;
    uint32_t l_dim = std::max<uint32_t>(1u, (header16 >> 12) & 7);
    double p_scale = ((header16 >> 6) & 63) / 63.0;
    double q_scale = (header16 & 63) / 63.0;

    uint32_t lx = is_landscape ? 7 : l_dim;
    uint32_t ly = is_landscape ? l_dim : 7;

    // Unpack L AC coefficients
    std::vector<double> l_coeffs(lx * ly, 0.0);
    l_coeffs[0] = l_dc;
    size_t byte_pos = 5;
    for (size_t i = 1; i < l_coeffs.size(); i += 2) {
        if (byte_pos >= hash_bytes.size()) break;
        uint8_t byte = hash_bytes[byte_pos++];
        uint8_t q1 = (byte >> 4) & 0xF;
        uint8_t q2 = byte & 0xF;

        l_coeffs[i] = ((q1 / 7.5) - 1.0) * l_scale;
        if (i + 1 < l_coeffs.size()) {
            l_coeffs[i + 1] = ((q2 / 7.5) - 1.0) * l_scale;
        }
    }

    // Unpack P AC coefficients (3x3 grid)
    std::vector<double> p_coeffs(9, 0.0);
    p_coeffs[0] = p_dc;
    for (size_t i = 1; i < p_coeffs.size(); i += 2) {
        if (byte_pos >= hash_bytes.size()) break;
        uint8_t byte = hash_bytes[byte_pos++];
        p_coeffs[i] = (((byte >> 4) & 0xF) / 7.5 - 1.0) * p_scale;
        if (i + 1 < p_coeffs.size()) {
            p_coeffs[i + 1] = ((byte & 0xF) / 7.5 - 1.0) * p_scale;
        }
    }

    // Unpack Q AC coefficients (3x3 grid)
    std::vector<double> q_coeffs(9, 0.0);
    q_coeffs[0] = q_dc;
    for (size_t i = 1; i < q_coeffs.size(); i += 2) {
        if (byte_pos >= hash_bytes.size()) break;
        uint8_t byte = hash_bytes[byte_pos++];
        q_coeffs[i] = (((byte >> 4) & 0xF) / 7.5 - 1.0) * q_scale;
        if (i + 1 < q_coeffs.size()) {
            q_coeffs[i + 1] = ((byte & 0xF) / 7.5 - 1.0) * q_scale;
        }
    }

    // Dimensions
    uint32_t out_w = (target_width > 0) ? target_width : 32;
    uint32_t out_h = (target_height > 0) ? target_height : 32;
    if (target_width == 0 && target_height == 0) {
        if (is_landscape) {
            out_w = 32;
            out_h = std::max(1u, static_cast<uint32_t>(std::round(32.0 * ly / lx)));
        } else {
            out_h = 32;
            out_w = std::max(1u, static_cast<uint32_t>(std::round(32.0 * lx / ly)));
        }
    }

    result.width = out_w;
    result.height = out_h;
    result.channels = 4;
    result.format = PixelFormat::RGBA8;
    result.data.resize(out_w * out_h * 4);

    // Inverse 2D DCT synthesis
    for (uint32_t y = 0; y < out_h; ++y) {
        for (uint32_t x = 0; x < out_w; ++x) {
            double l_val = 0.0;
            for (uint32_t v = 0; v < ly; ++v) {
                for (uint32_t u = 0; u < lx; ++u) {
                    double cu = (u == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
                    double cv = (v == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
                    l_val += cu * cv * l_coeffs[v * lx + u] *
                             std::cos(((2 * x + 1) * u * M_PI) / (2.0 * out_w)) *
                             std::cos(((2 * y + 1) * v * M_PI) / (2.0 * out_h));
                }
            }

            double p_val = 0.0;
            for (uint32_t v = 0; v < 3; ++v) {
                for (uint32_t u = 0; u < 3; ++u) {
                    double cu = (u == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
                    double cv = (v == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
                    p_val += cu * cv * p_coeffs[v * 3 + u] *
                             std::cos(((2 * x + 1) * u * M_PI) / (2.0 * out_w)) *
                             std::cos(((2 * y + 1) * v * M_PI) / (2.0 * out_h));
                }
            }

            double q_val = 0.0;
            for (uint32_t v = 0; v < 3; ++v) {
                for (uint32_t u = 0; u < 3; ++u) {
                    double cu = (u == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
                    double cv = (v == 0) ? (1.0 / std::sqrt(2.0)) : 1.0;
                    q_val += cu * cv * q_coeffs[v * 3 + u] *
                             std::cos(((2 * x + 1) * u * M_PI) / (2.0 * out_w)) *
                             std::cos(((2 * y + 1) * v * M_PI) / (2.0 * out_h));
                }
            }

            // Convert back to RGB
            double r = l_val + p_val / 3.0 + q_val / 2.0;
            double g = l_val + p_val / 3.0 - q_val / 2.0;
            double b = l_val - 2.0 * p_val / 3.0;

            uint8_t u_r = static_cast<uint8_t>(std::clamp(std::round(r * 255.0), 0.0, 255.0));
            uint8_t u_g = static_cast<uint8_t>(std::clamp(std::round(g * 255.0), 0.0, 255.0));
            uint8_t u_b = static_cast<uint8_t>(std::clamp(std::round(b * 255.0), 0.0, 255.0));
            uint8_t u_a = has_alpha ? 255 : 255;

            size_t idx = (y * out_w + x) * 4;
            result.data[idx] = u_r;
            result.data[idx + 1] = u_g;
            result.data[idx + 2] = u_b;
            result.data[idx + 3] = u_a;
        }
    }

    return result;
}

ImageBuffer ThumbHash::decode_from_base64(const std::string& base64_hash,
                                          uint32_t target_width,
                                          uint32_t target_height) {
    if (base64_hash.empty()) return {};
    auto raw_bytes = base64_decode(base64_hash);
    return decode(raw_bytes, target_width, target_height);
}

} // namespace image_odb::hash
