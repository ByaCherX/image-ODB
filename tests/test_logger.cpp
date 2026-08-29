#include "image_odb/logger.h"
#include <spdlog/spdlog.h>
#include <cassert>
#include <iostream>

void run_logger_tests() {
    // 1. Initial configuration
    image_odb::Logger::init();
    assert(image_odb::Logger::get_logger() != nullptr);
    assert(image_odb::Logger::get_level() == spdlog::level::info);

    // 2. Level string parsing
    assert(image_odb::Logger::parse_level("trace") == spdlog::level::trace);
    assert(image_odb::Logger::parse_level("TRACE") == spdlog::level::trace);
    assert(image_odb::Logger::parse_level("debug") == spdlog::level::debug);
    assert(image_odb::Logger::parse_level("DEBUG") == spdlog::level::debug);
    assert(image_odb::Logger::parse_level("info") == spdlog::level::info);
    assert(image_odb::Logger::parse_level("INFO") == spdlog::level::info);
    assert(image_odb::Logger::parse_level("warn") == spdlog::level::warn);
    assert(image_odb::Logger::parse_level("warning") == spdlog::level::warn);
    assert(image_odb::Logger::parse_level("WARN") == spdlog::level::warn);
    assert(image_odb::Logger::parse_level("error") == spdlog::level::err);
    assert(image_odb::Logger::parse_level("err") == spdlog::level::err);
    assert(image_odb::Logger::parse_level("critical") == spdlog::level::critical);
    assert(image_odb::Logger::parse_level("crit") == spdlog::level::critical);
    assert(image_odb::Logger::parse_level("fatal") == spdlog::level::critical);
    assert(image_odb::Logger::parse_level("off") == spdlog::level::off);
    assert(image_odb::Logger::parse_level("unknown_level") == spdlog::level::info); // Fallback

    // 3. Level to string
    assert(image_odb::Logger::level_to_string(spdlog::level::debug) == "debug");
    assert(image_odb::Logger::level_to_string(spdlog::level::info) == "info");
    assert(image_odb::Logger::level_to_string(spdlog::level::warn) == "warn");
    assert(image_odb::Logger::level_to_string(spdlog::level::err) == "error");

    // 4. configure_with string
    image_odb::Logger::configure_with("debug");
    assert(image_odb::Logger::get_level() == spdlog::level::debug);
    assert(spdlog::get_level() == spdlog::level::debug);

    image_odb::Logger::configure_with("warn");
    assert(image_odb::Logger::get_level() == spdlog::level::warn);
    assert(spdlog::get_level() == spdlog::level::warn);

    // 5. configure_with enum
    image_odb::Logger::configure_with(spdlog::level::trace);
    assert(image_odb::Logger::get_level() == spdlog::level::trace);

    // 6. set_level
    image_odb::Logger::set_level(spdlog::level::info);
    assert(image_odb::Logger::get_level() == spdlog::level::info);

    // 7. Verify logging calls
    spdlog::trace("Test trace message from unit test");
    spdlog::debug("Test debug message from unit test");
    spdlog::info("Test info message from unit test");
    spdlog::warn("Test warn message from unit test");
    spdlog::error("Test error message from unit test");

    // Reset back to info
    image_odb::Logger::set_level(spdlog::level::info);
}

