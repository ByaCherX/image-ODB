#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

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

} // namespace image_odb::util
