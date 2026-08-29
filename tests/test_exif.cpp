#include "image_odb/exif_reader.h"
#include <cassert>
#include <stdexcept>
#include <iostream>

void run_exif_tests() {
    using namespace image_odb::metadata;

    // Test 1: Standard EXIF date string parsing
    std::string date_str = "2026:08:24 15:30:45";
    auto tp = ExifReader::parse_exif_date(date_str);
    if (!tp.has_value()) {
        throw std::runtime_error("Failed to parse valid EXIF date");
    }

    // Test 2: ISO8601 formatting round-trip
    std::string formatted = ExifReader::format_iso8601(*tp);
    if (formatted.find("2026-08-24") == std::string::npos) {
        throw std::runtime_error("ISO8601 formatting mismatch: " + formatted);
    }

    // Test 3: Invalid date handling
    std::string invalid_date = "invalid_string";
    if (ExifReader::parse_exif_date(invalid_date).has_value()) {
        throw std::runtime_error("Invalid date string should have returned nullopt");
    }
}
