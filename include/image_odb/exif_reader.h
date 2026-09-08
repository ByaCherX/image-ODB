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
     * @brief Convert EXIF byte data into Photo metadata structure.
     * @param data Byte slice containing image data (JPEG) or EXIF segment (AVIF/APP1).
     * @param photo Photo object to populate.
     * @return True if conversion succeeded.
     */
    static bool exif_convert(std::span<const uint8_t> data, Photo& photo);

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
};

} // namespace image_odb::metadata
