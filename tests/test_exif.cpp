#include "image_odb/exif_reader.h"
#include "image_odb/util.h"
#include <cassert>
#include <stdexcept>
#include <iostream>

void run_exif_tests() {
    using namespace image_odb::metadata;
    using namespace image_odb::util;

    // Test 1: Standard EXIF date string parsing
    std::string date_str = "2026:08:24 15:30:45";
    auto tp = parse_exif_date(date_str);
    if (!tp.has_value()) {
        throw std::runtime_error("Failed to parse valid EXIF date");
    }

    // Test 2: ISO8601 formatting round-trip
    std::string formatted = format_iso8601(*tp);
    if (formatted.find("2026-08-24") == std::string::npos) {
        throw std::runtime_error("ISO8601 formatting mismatch: " + formatted);
    }

    // Test 3: Invalid date handling
    std::string invalid_date = "invalid_string";
    if (parse_exif_date(invalid_date).has_value()) {
        throw std::runtime_error("Invalid date string should have returned nullopt");
    }

    // Test 4: exif_convert with empty buffer
    std::vector<uint8_t> empty_buf;
    image_odb::Photo photo;
    bool ok_empty = ExifReader::exif_convert(empty_buf, photo);
    if (ok_empty) {
        throw std::runtime_error("exif_convert on empty buffer should return false");
    }

    // Test 5: parse_datestr - ISO date with space
    auto dt_space = parse_datestr("2024-08-15 13:45:20");
    if (!dt_space.has_value() || format_iso8601(*dt_space) != "2024-08-15T13:45:20Z") {
        throw std::runtime_error("parse_datestr failed for ISO datetime with space");
    }

    // Test 6: parse_datestr - ISO8601 with T and Z
    auto dt_iso = parse_datestr("2024-08-15T13:45:20Z");
    if (!dt_iso.has_value() || format_iso8601(*dt_iso) != "2024-08-15T13:45:20Z") {
        throw std::runtime_error("parse_datestr failed for ISO8601 with T and Z");
    }

    // Test 7: parse_datestr - ISO8601 with fractional seconds
    auto dt_frac = parse_datestr("2024-08-15T13:45:20.123456Z");
    if (!dt_frac.has_value() || format_iso8601(*dt_frac) != "2024-08-15T13:45:20Z") {
        throw std::runtime_error("parse_datestr failed for ISO8601 with fractional seconds");
    }

    // Test 8: parse_datestr - Date only (start of day, e.g. for --from)
    auto dt_from = parse_datestr("2024-08-15", /*end_of_day=*/false);
    if (!dt_from.has_value() || format_iso8601(*dt_from) != "2024-08-15T00:00:00Z") {
        throw std::runtime_error("parse_datestr failed for start of day date-only");
    }

    // Test 9: parse_datestr - Date only (end of day, e.g. for --to)
    auto dt_to = parse_datestr("2024-08-15", /*end_of_day=*/true);
    if (!dt_to.has_value() || format_iso8601(*dt_to) != "2024-08-15T23:59:59Z") {
        throw std::runtime_error("parse_datestr failed for end of day date-only");
    }

    // Test 10: parse_datestr - Slash separated format
    auto dt_slash = parse_datestr("2024/08/15");
    if (!dt_slash.has_value() || format_iso8601(*dt_slash) != "2024-08-15T00:00:00Z") {
        throw std::runtime_error("parse_datestr failed for slash date format");
    }

    // Test 11: parse_datestr - Dot separated format
    auto dt_dot = parse_datestr("2024.08.15 13:45:20");
    if (!dt_dot.has_value() || format_iso8601(*dt_dot) != "2024-08-15T13:45:20Z") {
        throw std::runtime_error("parse_datestr failed for dot datetime format");
    }

    // Test 12: parse_datestr - Compact 8-digit date
    auto dt_compact = parse_datestr("20240815");
    if (!dt_compact.has_value() || format_iso8601(*dt_compact) != "2024-08-15T00:00:00Z") {
        throw std::runtime_error("parse_datestr failed for compact date format");
    }

    // Test 13: parse_datestr - EXIF format fallback
    auto dt_exif_compat = parse_datestr("2024:08:15 13:45:20");
    if (!dt_exif_compat.has_value() || format_iso8601(*dt_exif_compat) != "2024-08-15T13:45:20Z") {
        throw std::runtime_error("parse_datestr failed for EXIF format string");
    }

    // Test 14: parse_datestr - Invalid dates
    if (parse_datestr("").has_value()) {
        throw std::runtime_error("parse_datestr on empty string should return nullopt");
    }
    if (parse_datestr("not-a-date").has_value()) {
        throw std::runtime_error("parse_datestr on arbitrary string should return nullopt");
    }
    if (parse_datestr("2024-02-30").has_value()) {
        throw std::runtime_error("parse_datestr on invalid calendar day (Feb 30) should return nullopt");
    }
}

