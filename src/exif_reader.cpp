#include "image_odb/exif_reader.h"
#include <tinyexif.h>
#include <fstream>
#include <iomanip>
#include <sstream>
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

} // namespace image_odb::metadata
