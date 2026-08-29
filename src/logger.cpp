#include "image_odb/logger.h"
#include <algorithm>
#include <cctype>
#include <mutex>

namespace image_odb {

namespace {

std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::shared_ptr<spdlog::logger> g_logger = nullptr;
std::once_flag g_init_flag;

} // namespace

void Logger::init() {
    std::call_once(g_init_flag, []() {
        // Check if logger already registered in spdlog registry
        g_logger = spdlog::get(DEFAULT_LOGGER_NAME);
        if (!g_logger) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern(DEFAULT_PATTERN);

            g_logger = std::make_shared<spdlog::logger>(DEFAULT_LOGGER_NAME, console_sink);
            spdlog::register_logger(g_logger);
        }

        g_logger->set_level(DEFAULT_LEVEL);
        g_logger->set_pattern(DEFAULT_PATTERN);
        spdlog::set_default_logger(g_logger);
        spdlog::set_level(DEFAULT_LEVEL);
        spdlog::set_pattern(DEFAULT_PATTERN);
    });
}

void Logger::configure_with(spdlog::level::level_enum level, std::string_view pattern) {
    init();
    if (g_logger) {
        std::string pat(pattern);
        g_logger->set_pattern(pat);
        g_logger->set_level(level);
        spdlog::set_pattern(pat);
        spdlog::set_level(level);
    }
}

void Logger::configure_with(std::string_view level_str, std::string_view pattern) {
    configure_with(parse_level(level_str), pattern);
}

void Logger::set_level(spdlog::level::level_enum level) {
    init();
    if (g_logger) {
        g_logger->set_level(level);
        spdlog::set_level(level);
    }
}

void Logger::set_level(std::string_view level_str) {
    set_level(parse_level(level_str));
}

spdlog::level::level_enum Logger::parse_level(std::string_view level_str) {
    std::string s = to_lower(level_str);
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "info") return spdlog::level::info;
    if (s == "warn" || s == "warning") return spdlog::level::warn;
    if (s == "err" || s == "error") return spdlog::level::err;
    if (s == "critical" || s == "crit" || s == "fatal") return spdlog::level::critical;
    if (s == "off") return spdlog::level::off;

    return spdlog::level::info;
}

std::string_view Logger::level_to_string(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace: return "trace";
        case spdlog::level::debug: return "debug";
        case spdlog::level::info: return "info";
        case spdlog::level::warn: return "warn";
        case spdlog::level::err: return "error";
        case spdlog::level::critical: return "critical";
        case spdlog::level::off: return "off";
        default: return "info";
    }
}

spdlog::level::level_enum Logger::get_level() {
    init();
    return g_logger ? g_logger->level() : DEFAULT_LEVEL;
}

std::shared_ptr<spdlog::logger> Logger::get_logger() {
    init();
    return g_logger;
}

} // namespace image_odb

