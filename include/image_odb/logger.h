#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>
#include <string_view>
#include <memory>

namespace image_odb {

/**
 * @brief Centralized Logger configuration and management for image-ODB.
 */
class Logger {
public:
    static constexpr const char* DEFAULT_LOGGER_NAME = "image_odb";
    static constexpr const char* DEFAULT_PATTERN = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v";
    static constexpr spdlog::level::level_enum DEFAULT_LEVEL = spdlog::level::info;

    /**
     * @brief Initialize logger with default colored console sink, Info level, and default pattern.
     */
    static void init();

    /**
     * @brief Configure logger with given log level enum and pattern.
     * @param level spdlog log level enum
     * @param pattern Custom spdlog formatting pattern (default colored: "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v")
     */
    static void configure_with(spdlog::level::level_enum level,
                               std::string_view pattern = DEFAULT_PATTERN);

    /**
     * @brief Configure logger using a level name string ("trace", "debug", "info", "warn", "error", "critical", "off").
     * @param level_str Level string (case-insensitive)
     * @param pattern Custom spdlog formatting pattern
     */
    static void configure_with(std::string_view level_str,
                               std::string_view pattern = DEFAULT_PATTERN);

    /**
     * @brief Set log level.
     */
    static void set_level(spdlog::level::level_enum level);

    /**
     * @brief Set log level from string.
     */
    static void set_level(std::string_view level_str);

    /**
     * @brief Parse string to spdlog::level::level_enum. Returns info if invalid.
     */
    static spdlog::level::level_enum parse_level(std::string_view level_str);

    /**
     * @brief Convert spdlog level to string representation.
     */
    static std::string_view level_to_string(spdlog::level::level_enum level);

    /**
     * @brief Get current active log level.
     */
    static spdlog::level::level_enum get_level();

    /**
     * @brief Retrieve underlying spdlog logger instance.
     */
    static std::shared_ptr<spdlog::logger> get_logger();
};

} // namespace image_odb

