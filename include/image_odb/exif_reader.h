#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <span>

namespace image_odb::metadata {

/**
 * @brief Utility for extracting EXIF, camera, lens, GPS, and optical metadata.
 */
class ExifReader {
public:
    /**
     * @brief Extract metadata from an image file on disk.
     * @param file_path Path to the image file (JPEG, AVIF, etc.).
     * @param photo Output photo object to populate with extracted metadata.
     * @return True if metadata was successfully parsed, false otherwise.
     */
    static bool read_from_file(const std::filesystem::path& file_path, Photo& photo);

    /**
     * @brief Extract metadata from an in-memory byte buffer.
     * @param buffer Byte buffer containing image data with EXIF header.
     * @param photo Output photo object to populate with extracted metadata.
     * @return True if metadata was successfully parsed, false otherwise.
     */
    static bool read_from_memory(std::span<const uint8_t> buffer, Photo& photo);

    /**
     * @brief Parse standard ISO8601 or EXIF format datetime string ("YYYY:MM:DD HH:MM:SS").
     * @param date_str The date string to parse.
     * @return Optional time_point if parsing succeeded.
     */
    static std::optional<std::chrono::system_clock::time_point> parse_exif_date(const std::string& date_str);

    /**
     * @brief Parse capture date from filename using common camera and phone naming conventions.
     * Examples: IMG_20240815_134520.jpg, 2024-08-15_13-45-20.jpg, Screenshot_20240815-134520.png, 20240815_134520.jpg
     * @param file_path Path to the image file.
     * @return Optional time_point if date pattern was recognized.
     */
    static std::optional<std::chrono::system_clock::time_point> parse_date_from_filename(const std::filesystem::path& file_path);

    /**
     * @brief Extract last write/modification time of file on disk as a fallback time_point.
     * @param file_path Path to the file.
     * @return Optional time_point.
     */
    static std::optional<std::chrono::system_clock::time_point> get_file_modification_date(const std::filesystem::path& file_path);

    /**
     * @brief Format a time_point to ISO8601 UTC string ("YYYY-MM-DDTHH:MM:SSZ").
     * @param tp Time point.
     * @return Formatted ISO8601 string.
     */
    static std::string format_iso8601(const std::chrono::system_clock::time_point& tp);
};

} // namespace image_odb::metadata
