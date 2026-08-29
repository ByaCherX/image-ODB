add_executable(test_image_odb
    test_main.cpp
    test_logger.cpp
    test_phash.cpp
    test_thumbhash.cpp
    test_avif.cpp
    test_db.cpp
    test_exif.cpp
    test_cache.cpp
    test_pipeline.cpp
)

target_link_libraries(test_image_odb
    PRIVATE
        image_odb
        spdlog::spdlog
)

if(MSVC)
    target_compile_options(test_image_odb PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(test_image_odb PRIVATE NOMINMAX)
else()
    target_compile_options(test_image_odb PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME image_odb_unit_tests COMMAND test_image_odb)
