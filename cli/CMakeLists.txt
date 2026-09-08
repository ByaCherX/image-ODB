add_executable(image_cli
    main.cpp
    commands/common.cpp
    commands/init.cpp
    commands/image.cpp
    commands/scan.cpp
    commands/list.cpp
    commands/extract.cpp
    commands/preview.cpp
    commands/cache.cpp
)

target_include_directories(image_cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(image_cli
    PRIVATE
        image_odb
        CLI11::CLI11
        spdlog::spdlog
)

if(MSVC)
    target_compile_options(image_cli PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(image_cli PRIVATE NOMINMAX)
else()
    target_compile_options(image_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
