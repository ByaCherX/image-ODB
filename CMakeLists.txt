cmake_minimum_required(VERSION 3.24)

project(image_odb
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "High-performance photo database and AVIF multi-frame archiving library & CLI tool"
)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to build tests and CLI
option(IMAGE_ODB_BUILD_CLI "Build image-cli command line utility" ON)
option(IMAGE_ODB_BUILD_TESTS "Build unit and integration tests" ON)

# Find Required Dependencies via vcpkg
find_package(libavif CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(TinyEXIF CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)

if(IMAGE_ODB_BUILD_CLI)
    find_package(CLI11 CONFIG REQUIRED)
endif()

# Core Library Definition
add_library(image_odb
    src/logger.cpp
    src/core/engine.cpp
    src/core/pipeline.cpp
    src/exif_reader.cpp
    src/image_codec.cpp
    src/avif_codec.cpp
    src/jpeg_codec.cpp
    src/similarity_engine.cpp
    src/database.cpp
    src/lru_cache.cpp
    src/disk_cache.cpp
    src/cache_manager.cpp
    src/phash.cpp
    src/thumbhash.cpp
)

target_include_directories(image_odb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(image_odb
    PUBLIC
        nlohmann_json::nlohmann_json
        spdlog::spdlog
    PRIVATE
        avif
        JPEG::JPEG
        SQLiteCpp
        TinyEXIF::TinyEXIF
        BLAKE3::blake3
)

# Compiler specific flags and definitions
if(MSVC)
    target_compile_options(image_odb PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(image_odb PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(image_odb PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Subdirectories
if(IMAGE_ODB_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(IMAGE_ODB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
