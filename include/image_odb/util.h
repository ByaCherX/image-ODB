#pragma once

#include <cstdint>
#include <cstring>
#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <chrono>
#include <optional>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace image_odb::util {

/**
 * @brief Checks whether the byte span starts with the specified string_view/bytes prefix.
 * @param data Byte slice to inspect.
 * @param prefix Target prefix sequence.
 * @return True if data begins with prefix.
 */
inline bool starts_with(std::span<const uint8_t> data, std::string_view prefix) noexcept {
    return data.size() >= prefix.size() &&
           std::memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

/**
 * @brief Checks whether the byte span starts with another byte span prefix.
 * @param data Byte slice to inspect.
 * @param prefix Target byte prefix slice.
 * @return True if data begins with prefix.
 */
inline bool starts_with(std::span<const uint8_t> data, std::span<const uint8_t> prefix) noexcept {
    return data.size() >= prefix.size() &&
           std::memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

/**
 * @brief Constructs a UTC system_clock::time_point from calendar components.
 * Validates year, month, day, leap years, and time boundaries.
 */
inline std::optional<std::chrono::system_clock::time_point> make_time_point(
    int year, unsigned month, unsigned day,
    int hour = 0, int min = 0, int sec = 0) noexcept {

    if (year < 1970 || year > 2200 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return std::nullopt;
    }

    const std::chrono::year_month_day ymd{
        std::chrono::year{year},
        std::chrono::month{month},
        std::chrono::day{day}
    };
    if (!ymd.ok()) {
        return std::nullopt;
    }

#if defined(_WIN32)
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = static_cast<int>(month) - 1;
    tm.tm_mday = static_cast<int>(day);
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = 0;
    time_t t = _mkgmtime(&tm);
    if (t == -1) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
#else
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = static_cast<int>(month) - 1;
    tm.tm_mday = static_cast<int>(day);
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = 0;
    time_t t = timegm(&tm);
    if (t == -1) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
#endif
}

/**
 * @brief Convert std::tm (interpreted as UTC) to system_clock::time_point.
 */
inline std::optional<std::chrono::system_clock::time_point> tm_to_time_point(const std::tm& tm) noexcept {
    return make_time_point(
        tm.tm_year + 1900,
        static_cast<unsigned>(tm.tm_mon + 1),
        static_cast<unsigned>(tm.tm_mday),
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    );
}

/**
 * @brief Format a time_point to ISO8601 UTC string ("YYYY-MM-DDTHH:MM:SSZ").
 */
inline std::string format_iso8601(const std::chrono::system_clock::time_point& tp) {
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

/**
 * @brief Parse standard EXIF datetime string ("YYYY:MM:DD HH:MM:SS" or ISO format).
 * @param date_str Date string from EXIF metadata.
 * @return Optional time_point if parsed successfully.
 */
inline std::optional<std::chrono::system_clock::time_point> parse_exif_date(std::string_view date_str) {
    while (!date_str.empty() && (std::isspace(static_cast<unsigned char>(date_str.front())) || date_str.front() == '\0')) {
        date_str.remove_prefix(1);
    }
    while (!date_str.empty() && (std::isspace(static_cast<unsigned char>(date_str.back())) || date_str.back() == '\0')) {
        date_str.remove_suffix(1);
    }
    if (date_str.size() < 19) return std::nullopt;

    std::string s(date_str);
    static constexpr const char* EXIF_FORMATS[] = {
        "%Y:%m:%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M:%S"
    };

    for (const char* fmt : EXIF_FORMATS) {
        std::tm tm{};
        std::istringstream ss(s);
        ss >> std::get_time(&tm, fmt);
        if (!ss.fail()) {
            return tm_to_time_point(tm);
        }
    }
    return std::nullopt;
}

/**
 * @brief Parse general date or datetime string (non-EXIF).
 * Builtin standard parsing using std::get_time. Supports ISO8601, slash/dot formats,
 * compact dates, and optional time.
 * @param date_str Input date string.
 * @param end_of_day If true and input has date only, sets time to 23:59:59 UTC (for --to filter).
 *                   If false, sets time to 00:00:00 UTC (for --from filter).
 * @return Optional time_point if parsed successfully.
 */
inline std::optional<std::chrono::system_clock::time_point> parse_datestr(
    std::string_view date_str, bool end_of_day = false) {

    while (!date_str.empty() && (std::isspace(static_cast<unsigned char>(date_str.front())) || date_str.front() == '\0')) {
        date_str.remove_prefix(1);
    }
    while (!date_str.empty() && (std::isspace(static_cast<unsigned char>(date_str.back())) || date_str.back() == '\0')) {
        date_str.remove_suffix(1);
    }
    if (date_str.empty()) return std::nullopt;

    std::string s(date_str);

    // Strip fractional seconds (e.g. .123 or .123456) before 'Z' or end of string
    auto dot_pos = s.rfind('.');
    if (dot_pos != std::string::npos && dot_pos >= 10) {
        auto end_frac = dot_pos + 1;
        while (end_frac < s.size() && std::isdigit(static_cast<unsigned char>(s[end_frac]))) {
            ++end_frac;
        }
        if (end_frac > dot_pos + 1) {
            s.erase(dot_pos, end_frac - dot_pos);
        }
    }

    struct FormatDesc {
        const char* fmt;
        bool has_time;
    };

    static constexpr FormatDesc FORMATS[] = {
        // Full datetime formats
        {"%Y-%m-%d %H:%M:%S",   true},
        {"%Y-%m-%dT%H:%M:%SZ",  true},
        {"%Y-%m-%dT%H:%M:%S",   true},
        {"%Y/%m/%d %H:%M:%S",   true},
        {"%Y.%m.%d %H:%M:%S",   true},
        {"%Y:%m:%d %H:%M:%S",   true},
        {"%Y%m%d_%H%M%S",       true},
        {"%Y%m%d-%H%M%S",       true},
        {"%Y%m%d%H%M%S",        true},
        // Date-only formats
        {"%Y-%m-%d",            false},
        {"%Y/%m/%d",            false},
        {"%Y.%m.%d",            false},
        {"%Y:%m:%d",            false},
        {"%Y%m%d",              false},
    };

    for (const auto& entry : FORMATS) {
        std::tm tm{};
        if (!entry.has_time && end_of_day) {
            tm.tm_hour = 23;
            tm.tm_min = 59;
            tm.tm_sec = 59;
        }
        std::istringstream ss(s);
        ss >> std::get_time(&tm, entry.fmt);
        if (!ss.fail()) {
            std::string remaining;
            ss >> remaining;
            if (remaining.empty() || remaining == "Z" || remaining == "z") {
                auto tp = tm_to_time_point(tm);
                if (tp.has_value()) {
                    return tp;
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace image_odb::util
