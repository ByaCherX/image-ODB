#include "image_odb/exif_reader.h"
#include <tinyexif.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <regex>
#include <spdlog/spdlog.h>

namespace image_odb::metadata {

bool ExifReader::read_from_file(const std::filesystem::path& file_path, Photo& photo) {
    std::ifstream stream(file_path, std::ios::binary);
    if (!stream.is_open()) {
        spdlog::warn("Cannot open file for EXIF reading: {}", file_path.string());
        return false;
    }

    TinyEXIF::EXIFInfo info(stream);
    if (!info.Fields) {
        spdlog::debug("No EXIF metadata found in: {}", file_path.string());
        return false;
    }

    // Camera details
    photo.camera.make = info.Make;
    photo.camera.model = info.Model;
    photo.camera.serial_number = info.SerialNumber;

    // Lens details
    photo.lens.make = info.LensInfo.Make;
    photo.lens.model = info.LensInfo.Model;
    if (info.LensInfo.FocalLengthIn35mm > 0.0) {
        photo.lens.focal_length_in_35mm = info.LensInfo.FocalLengthIn35mm;
    }
    if (info.FocalLength > 0.0) {
        photo.lens.focal_length_mm = info.FocalLength;
    }

    // Exposure parameters
    if (info.FNumber > 0.0) {
        photo.exposure.f_number = info.FNumber;
    }
    if (info.ExposureTime > 0.0) {
        photo.exposure.exposure_time_sec = info.ExposureTime;
        std::ostringstream ss;
        if (info.ExposureTime < 1.0 && info.ExposureTime > 0.0) {
            ss << "1/" << static_cast<uint32_t>(std::round(1.0 / info.ExposureTime));
        } else {
            ss << std::fixed << std::setprecision(2) << info.ExposureTime << "s";
        }
        photo.exposure.exposure_time_str = ss.str();
    }
    if (info.ISOSpeedRatings > 0) {
        photo.exposure.iso_speed = info.ISOSpeedRatings;
    }
    if (info.ExposureBiasValue != 0.0) {
        photo.exposure.exposure_bias = info.ExposureBiasValue;
    }
    photo.exposure.flash_fired = ((info.Flash & 1) != 0);

    // Dimensions & Orientation
    if (info.ImageWidth > 0) photo.dimensions.width = info.ImageWidth;
    if (info.ImageHeight > 0) photo.dimensions.height = info.ImageHeight;
    if (info.Orientation > 0) photo.dimensions.orientation = info.Orientation;

    // Geolocation
    if (info.GeoLocation.hasLatLon()) {
        photo.location.latitude = info.GeoLocation.Latitude;
        photo.location.longitude = info.GeoLocation.Longitude;
        if (info.GeoLocation.hasAltitude()) {
            photo.location.altitude = info.GeoLocation.Altitude;
        }
    }

    // Timestamp
    if (!info.DateTimeOriginal.empty()) {
        photo.capture_date = parse_exif_date(info.DateTimeOriginal);
    } else if (!info.DateTime.empty()) {
        photo.capture_date = parse_exif_date(info.DateTime);
    }

    // Extra JSON payload
    nlohmann::json extra;
    extra["software"] = info.Software;
    extra["copyright"] = info.Copyright;
    extra["description"] = info.ImageDescription;

    // Date fallback if EXIF timestamp missing or unparseable
    if (!photo.capture_date.has_value()) {
        if (auto fn_date = parse_date_from_filename(file_path); fn_date.has_value()) {
            photo.capture_date = fn_date;
            extra["date_source"] = "filename";
        } else if (auto fs_date = get_file_modification_date(file_path); fs_date.has_value()) {
            photo.capture_date = fs_date;
            extra["date_source"] = "filesystem";
        }
    }

    photo.exif_json = extra;

    return true;
}

bool ExifReader::read_from_memory(std::span<const uint8_t> buffer, Photo& photo) {
    if (buffer.empty()) return false;
    TinyEXIF::EXIFInfo info(buffer.data(), static_cast<unsigned>(buffer.size()));
    if (!info.Fields) return false;

    photo.camera.make = info.Make;
    photo.camera.model = info.Model;
    if (info.ImageWidth > 0) photo.dimensions.width = info.ImageWidth;
    if (info.ImageHeight > 0) photo.dimensions.height = info.ImageHeight;
    return true;
}

std::optional<std::chrono::system_clock::time_point> ExifReader::parse_exif_date(const std::string& date_str) {
    // Format: "YYYY:MM:DD HH:MM:SS" or ISO8601 "YYYY-MM-DDTHH:MM:SS"
    if (date_str.size() < 19) return std::nullopt;

    std::tm tm{};
    std::istringstream ss(date_str);
    char sep1, sep2, sep3, sep4;
    int year, month, day, hour, min, sec;

    if (date_str[4] == ':') {
        // EXIF format
        ss >> year >> sep1 >> month >> sep2 >> day >> hour >> sep3 >> min >> sep4 >> sec;
    } else {
        // Standard ISO format
        ss >> year >> sep1 >> month >> sep2 >> day >> sep3 >> hour >> sep4 >> min >> sep4 >> sec;
    }

    if (ss.fail()) return std::nullopt;

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;

#if defined(_WIN32)
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    if (t == -1) return std::nullopt;

    return std::chrono::system_clock::from_time_t(t);
}

std::string ExifReader::format_iso8601(const std::chrono::system_clock::time_point& tp) {
    time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::optional<std::chrono::system_clock::time_point> ExifReader::parse_date_from_filename(const std::filesystem::path& file_path) {
    std::string stem = file_path.stem().string();

    // Pattern 1: IMG_20240815_134520, 20240815_134520, 2024-08-15_13-45-20, 2024.08.15 13.45.20, Screenshot_20240815-134520, PXL_20240815_134520
    static const std::regex dt_full_regex(
        R"((?:IMG_|VID_|PXL_|Screenshot_|DSC_|WP_)?(\d{4})[-_.]?(\d{2})[-_.]?(\d{2})[-_ T](\d{2})[-_.:]?(\d{2})[-_.:]?(\d{2}))",
        std::regex::optimize
    );

    std::smatch match;
    if (std::regex_search(stem, match, dt_full_regex)) {
        try {
            int year = std::stoi(match[1].str());
            int month = std::stoi(match[2].str());
            int day = std::stoi(match[3].str());
            int hour = std::stoi(match[4].str());
            int min = std::stoi(match[5].str());
            int sec = std::stoi(match[6].str());

            if (year >= 1970 && year <= 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
                hour >= 0 && hour <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 59) {
                std::tm tm{};
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                tm.tm_hour = hour;
                tm.tm_min = min;
                tm.tm_sec = sec;
                tm.tm_isdst = -1;

#if defined(_WIN32)
                time_t t = _mkgmtime(&tm);
#else
                time_t t = timegm(&tm);
#endif
                if (t != -1) {
                    return std::chrono::system_clock::from_time_t(t);
                }
            }
        } catch (...) {}
    }

    // Pattern 2: Date only like 2024-08-15 or 20240815
    static const std::regex d_only_regex(
        R"((?:IMG_|VID_|PXL_|Screenshot_|DSC_|WP_)?(\d{4})[-_.]?(\d{2})[-_.]?(\d{2}))",
        std::regex::optimize
    );

    if (std::regex_search(stem, match, d_only_regex)) {
        try {
            int year = std::stoi(match[1].str());
            int month = std::stoi(match[2].str());
            int day = std::stoi(match[3].str());

            if (year >= 1970 && year <= 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
                std::tm tm{};
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                tm.tm_hour = 12;
                tm.tm_min = 0;
                tm.tm_sec = 0;
                tm.tm_isdst = -1;

#if defined(_WIN32)
                time_t t = _mkgmtime(&tm);
#else
                time_t t = timegm(&tm);
#endif
                if (t != -1) {
                    return std::chrono::system_clock::from_time_t(t);
                }
            }
        } catch (...) {}
    }

    return std::nullopt;
}

std::optional<std::chrono::system_clock::time_point> ExifReader::get_file_modification_date(const std::filesystem::path& file_path) {
    try {
        if (!std::filesystem::exists(file_path)) return std::nullopt;
        auto ftime = std::filesystem::last_write_time(file_path);
        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        return sctp;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace image_odb::metadata
