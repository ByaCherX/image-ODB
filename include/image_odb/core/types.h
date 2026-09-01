#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace image_odb {

/**
 * @brief Canonical format identifiers for image containers and codecs.
 */
enum class ImageFormat {
    UNKNOWN,
    JPEG,
    AVIF,
    PNG,
    WEBP,
    TIFF,
    BMP
};
/**
 * @brief Cache operating mode controlling RAM LRU and Disk thumbnail cache behavior.
 */
enum class CacheMode {
    ALL,       /**< Both RAM LRU and Disk thumbnail cache enabled (default). */
    DISK_ONLY, /**< Only Disk thumbnail cache enabled (.photo_cache), RAM LRU bypassed. */
    RAM_ONLY,  /**< Only in-memory RAM LRU cache enabled, Disk cache bypassed. */
    NONE       /**< All caching disabled (no disk I/O, no RAM LRU). */
};

/**
 * @brief In-memory uncompressed pixel memory layouts, channel configurations, and data representations.
 * PixelFormat dictates how pixel components are laid out in contiguous memory (interleaved vs planar),
 * the number of color channels, and the byte size per pixel element.
 */
enum class PixelFormat {
    RGB8,     /**< 24-bit Standard RGB (3 bytes per pixel). */
    RGBA8,    /**< 32-bit RGBA with Alpha transparency (4 bytes per pixel). */
    GRAY8,    /**< 8-bit Grayscale / Luminance (1 byte per pixel). */
    RGB16,    /**< 48-bit Deep Color RGB (6 bytes per pixel, 16-bit uint per channel). */
    RGBA16,   /**< 64-bit Deep Color RGBA with Alpha (8 bytes per pixel, 16-bit uint per channel). */
    RGBA_F16  /**< 64-bit Half-Float RGBA (8 bytes per pixel, 16-bit IEEE 754 half-precision float per channel). */
};

/**
 * @brief Chroma Subsampling modes for lossy YCbCr encoders (AVIF, JPEG).
 * Exploits the biological characteristic of human vision being significantly more sensitive
 * to brightness variations (Luminance - Y) than color variations (Chrominance - Cb/Cr).
 */
enum class ChromaSubsampling {
    YUV420, /**< 4:2:0 Chroma Subsampling (Quarter Chroma Resolution). */
    YUV422, /**< 4:2:2 Chroma Subsampling (Half Chroma Resolution). */
    YUV444, /**< 4:4:4 Full Chroma Resolution (No Subsampling). */
    YUV400  /**< 4:0:0 Grayscale / Monochrome. */
};

/**
 * @brief Color Primaries defining the chromaticity coordinates (CIE 1931 xy) of RGB color gamuts.
 *
 * @details
 * Standardized according to ITU-T H.273 / ISO/IEC 23091-2 (CICP - Coding-Independent Code Points).
 */
enum class ColorPrimaries : uint8_t {
    BT709_sRGB = 1,  /**< ITU-R BT.709-6 / IEC 61966-2-1 (sRGB). */
    Unspecified = 2, /**< Unspecified / Generic color primaries. */
    BT2020 = 9,      /**< ITU-R BT.2020 / BT.2100 Wide Color Gamut (WCG). */
    DCI_P3 = 12      /**< SMPTE EG 432-1 (Display P3 / DCI-P3). */
};

/**
 * @brief Transfer Characteristics defining the Opto-Electronic / Electro-Optical Transfer Function (OETF / EOTF).
 * Standardized according to ITU-T H.273 / ISO/IEC 23091-2 (CICP). Defines the non-linear curve
 * relating encoded numerical pixel values to physical optical luminance.
 */
enum class TransferCharacteristics : uint8_t {
    sRGB = 13,  /**< IEC 61966-2-1 sRGB / BT.709 transfer characteristic (piecewise linear + gamma ~2.2). */
    Linear = 8, /**< Linear transfer characteristic (L = V). */
    PQ = 16,    /**< SMPTE ST 2084 Perceptual Quantizer (PQ). */
    HLG = 18    /**< ITU-R BT.2100 Hybrid Log-Gamma (HLG). */
};

/**
 * @brief Color metadata attached to an image buffer specifying color space and dynamic range.
 */
struct ColorProfile {
    /** @brief Chromaticity color gamut definition. Default: BT.709 / sRGB. */
    ColorPrimaries primaries{ColorPrimaries::BT709_sRGB};

    /** @brief Non-linear luminance transfer characteristic. Default: sRGB. */
    TransferCharacteristics transfer{TransferCharacteristics::sRGB};

    /**
     * @brief Check whether this color profile indicates High Dynamic Range (HDR) content.
     * @return True if transfer function is Perceptual Quantizer (PQ) or Hybrid Log-Gamma (HLG).
     */
    [[nodiscard]] bool is_hdr() const noexcept {
        return transfer == TransferCharacteristics::PQ || transfer == TransferCharacteristics::HLG;
    }
};

/**
 * @brief Raw uncompressed in-memory raster image buffer.
 *
 * @details
 * Represents a 2D image matrix stored in row-major order in continuous heap memory.
 * Provides direct access to raw bytes, pixel formatting parameters, and color space metadata.
 */
struct ImageBuffer {
    uint32_t width{0}; /**< Stored image width */
    uint32_t height{0}; /**< Stored image height */
    uint32_t channels{0}; /**< Number of interleaved color channels*/
    PixelFormat format{PixelFormat::RGB8}; /**< Memory layout and data type specification */
    ColorProfile color_profile; /**< Color gamut and transfer function metadata */
    std::vector<uint8_t> data; /**< Contiguous raw byte storage holding interleaved pixel data */

    [[nodiscard]] uint32_t bit_depth() const noexcept {
        switch (format) {
        case PixelFormat::RGB16:
        case PixelFormat::RGBA16:
        case PixelFormat::RGBA_F16: return 16;
        default: return 8;
        }
    }

    [[nodiscard]] uint32_t pixel_count() const noexcept { return width * height; }
    [[nodiscard]] bool empty() const noexcept {  return data.empty() || width == 0 || height == 0; }
    [[nodiscard]] size_t size_bytes() const noexcept { return data.size(); }
};

/**
 * @brief Fine-grained configuration parameters for image encoding in ImageCodec and AvifCodec.
 */
struct EncodeOptions {
    ImageFormat format{ImageFormat::AVIF}; /**< Output container format */
    int quality{80}; /**< Lossy compression quality factor (1-100) */
    int speed{6}; /**< CPU encoding speed vs compression density trade-off (0-10) */
    ChromaSubsampling subsampling{ChromaSubsampling::YUV420}; /**< Chroma subsampling mode */
    int bit_depth{8}; /**< Bit depth for AVIF encoder (8, 10, or 12 bits) */
    bool lossless{false}; /**< when true, enables lossless compression mode */
};

/**
 * @brief Fine-grained configuration parameters for image decoding in ImageCodec and JpegCodec.
 */
struct DecodeOptions {
    std::optional<PixelFormat> target_format; /**< Optional requested target pixel format */
    bool apply_exif_orientation{true}; /**< Automatically apply EXIF orientation during decode. */
    /**
     * @brief Fast hardware/DCT downscale factor during decod (1 = 1:1, n = 1/n)
     * Accelerates thumbnail synthesis by decoding directly at reduced resolution. Default: 1.
     */
    uint32_t downscale_factor{1};
};

/**
 * @brief Metadata describing the camera body hardware.
 */
struct CameraInfo {
    std::string make; /**< Camera manufacturer / make (e.g., "Sony", "Canon", "Apple") */
    std::string model; /**< Camera body model name (e.g., "ILCE-7RM4", "iPhone 15 Pro") */
    std::string serial_number; /**< Unique hardware serial number extracted from EXIF maker notes. */
};

/**
 * @brief Optical characteristics and lens hardware metadata.
 */
struct LensInfo {
    std::string make;   /**< Lens manufacturer (e.g., "Sony", "Sigma", "Canon"). */
    std::string model;  /**< Lens model designation (e.g., "FE 24-70mm F2.8 GM II"). */
    std::optional<double> focal_length_mm; /**< Actual physical optical focal length in millimeters (e.g., 35.0 mm). */
    std::optional<double> focal_length_in_35mm; /**< 35mm full-frame sensor equivalent focal length in millimeters (e.g., 52.5 mm on APS-C). */
};

/**
 * @brief Photometric exposure settings and camera capture parameters.
 */
struct ExposureInfo {
    std::optional<double> f_number;          /**< Lens aperture f-number (e.g., 2.8 for f/2.8, 1.4 for f/1.4). */
    std::string exposure_time_str;           /**< Human-readable shutter speed fraction string (e.g., "1/2000", "1/60", "0.5"). */
    std::optional<double> exposure_time_sec; /**< Exact shutter duration in seconds (e.g., 0.0005 for 1/2000s). */
    std::optional<uint32_t> iso_speed;       /**< ISO film / sensor sensitivity rating (e.g., 100, 800, 3200). */
    std::optional<double> exposure_bias;     /**< Exposure compensation bias in Exposure Value (EV) units (e.g., -0.67 EV, +1.0 EV). */
    bool flash_fired{false};                 /**< Flag indicating whether a physical strobe flash was fired during exposure. */
};

/**
 * @brief Geographic WGS 84 coordinates and reverse-geocoded location data.
 */
struct GeoLocation {
    std::optional<double> latitude;     /**< Latitude in decimal degrees (-90.0 to +90.0, e.g., 41.0082). */
    std::optional<double> longitude;    /**< Longitude in decimal degrees (-180.0 to +180.0, e.g., 28.9784). */
    std::optional<double> altitude;     /**< Altitude above mean sea level in meters. */
    std::string place_name;             /**< Human-readable place or administrative area name. */
};

/**
 * @brief Spatial geometric dimensions and EXIF orientation tag.
 */
struct ImageDimensions {
    uint32_t width{0};          /**< Image width in pixels. */
    uint32_t height{0};         /**< Image height in pixels. */
    /**
     * Standard EXIF orientation tag (
     *   1 = Top-Left (0°), 
     *   3 = Bottom-Right (180°), 
     *   6 = Right-Top (90° CW), 
     *   8 = Left-Bottom (270° CW)
     * ).
     */
    uint16_t orientation{1};
};

/**
 * @brief Represents an individual frame within an AVIF multi-frame burst sequence container.
 */
struct BurstFrame {
    int64_t id{-1};             /**< Primary key ID in the database table (-1 if unpersisted). */
    int64_t photo_id{-1};       /**< Foreign key ID referencing the parent burst Photo container record. */
    uint32_t frame_index{0};    /**< Zero-based sequential frame index within the multi-frame container. */
    std::string original_file_name; /**< Original source filename prior to burst compression archiving. */
    uint32_t width{0};          /**< Width of this individual frame in pixels. */
    uint32_t height{0};         /**< Height of this individual frame in pixels. */
    uint64_t phash{0};          /**< 64-bit DCT-based perceptual hash computed for this frame. */
    std::string thumbhash;      /**< Base64-encoded ThumbHash string for high-speed placeholder rendering. */
    nlohmann::json exif_json;   /**< Complete raw EXIF key-value metadata payload parsed for this frame. */
};

/**
 * @brief Primary database entity representing an ingested photograph or AVIF burst container.
 */
struct Photo {
    // Identity & Storage
    int64_t id{-1};         /**< Database primary key ID (-1 if unpersisted). */
    std::filesystem::path file_path; /**< Filesystem path to the stored photo or AVIF burst container file. */
    uint64_t file_size{0};  /**< Total file size on disk in bytes. */
    std::string hash;       /**< Cryptographic BLAKE3 hash of the file payload for deduplication. */
    
    // Geometry & Format
    ImageDimensions dimensions; /**< Pixel dimensions and EXIF orientation. */
    std::string mime_type;      /**< MIME content type string (e.g., "image/jpeg", "image/avif"). */
 
    // Timestamp & Geolocation
    std::optional<std::chrono::system_clock::time_point> capture_date; /**< UTC capture date and time recorded in EXIF metadata. */
    GeoLocation location; /**< GPS coordinates and reverse-geocoded place information. */
 
    // Hardware & Optics
    CameraInfo camera;      /**< Camera body hardware properties. */
    LensInfo lens;          /**< Lens and optical focal properties. */
    ExposureInfo exposure;  /**< Photometric exposure settings. */
 
    // Perceptual Hashing & Thumbnail Preview
    uint64_t phash{0};      /**< 64-bit DCT perceptual hash for fast visual similarity indexing. */
    std::string thumbhash;  /**< Compact base64-encoded ThumbHash string representation. */
 
    // Multi-frame / Burst Information
    bool is_burst_group{false};     /**< True if this entity is a multi-frame AVIF burst group container. */
    uint32_t frame_count{1};        /**< Total frame count (1 for still photos, >= 2 for burst containers). */
    std::vector<BurstFrame> frames; /**< Collection of child burst frame records if is_burst_group is true. */
 
    // Extended / Raw EXIF & Timestamps
    nlohmann::json exif_json; /**< Structured JSON object holding all extracted EXIF/TIFF tags. */
    /**
     * Ingestion timestamp when this record was added to the database.
     */
    std::chrono::system_clock::time_point created_at{std::chrono::system_clock::now()};
};
 
/**
 * @brief Configuration parameters for directory scanning, ingestion, and burst clustering.
 */
struct ScanOptions {
    bool group_bursts{false}; /**< Automatically detect and group rapid burst sequences into AVIF containers. Default: false. */
    uint32_t max_threads{0}; /**< Maximum parallel worker threads (0 = automatic hardware concurrency). Default: 0. */
    uint32_t burst_time_window_seconds{3}; /**< Maximum elapsed time in seconds between consecutive shots to qualify as a burst. Default: 3s. */
    uint32_t burst_max_hamming_distance{5}; /**< Maximum Hamming distance between pHashes to qualify as the same burst scene. Default: 5. */
    bool recursive{true}; /**< Recursively traverse nested subdirectories during file discovery. Default: true. */
    bool generate_previews{true}; /**< Generate and cache downscaled thumbnail previews during ingestion. Default: true. */
};

/**
 * @brief Filtering, sorting, and pagination criteria for querying photo records from the database.
 */
struct ListOptions {
    std::optional<std::string> location_filter; /**< Filter by reverse-geocoded location substring. */
    std::optional<std::string> camera_make_filter; /**< Filter by camera manufacturer substring. */
    std::optional<std::string> camera_model_filter; /**< Filter by camera model substring. */
    std::optional<std::string> lens_filter; /**< Filter by lens model substring. */
    std::optional<std::chrono::system_clock::time_point> date_from; /**< Filter photos captured on or after this UTC timestamp. */
    std::optional<std::chrono::system_clock::time_point> date_to; /**< Filter photos captured on or before this UTC timestamp. */
    std::optional<bool> burst_only; /**< If set to true, only return multi-frame burst containers. */
    std::optional<uint32_t> min_iso; /**< Minimum ISO speed rating threshold. */
    std::optional<uint32_t> max_iso; /**< Maximum ISO speed rating threshold. */
    std::optional<double> min_focal_length; /**< Minimum focal length in millimeters. */
    std::optional<double> max_focal_length; /**< Maximum focal length in millimeters. */
    std::optional<double> min_f_number; /**< Minimum aperture f-number threshold. */
    std::optional<double> max_f_number; /**< Maximum aperture f-number threshold. */
    std::string sort_by{"capture_date"}; /**< Database column to sort results by ("capture_date", "created_at", "file_size", "iso_speed", "f_number"). Default: "capture_date". */
    bool ascending{false}; /**< Sort direction: true for ascending (A-Z, oldest first), false for descending (newest first). Default: false. */
    uint32_t limit{100}; /**< Maximum number of records to return (pagination page size). Default: 100. */
    uint32_t offset{0}; /**< Number of records to skip (pagination offset). Default: 0. */
};

} // namespace image_odb
