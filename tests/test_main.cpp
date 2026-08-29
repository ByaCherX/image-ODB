#include <iostream>
#include <vector>
#include <functional>
#include <string>

// Test function declarations
void run_logger_tests();
void run_phash_tests();
void run_thumbhash_tests();
void run_avif_codec_tests();
void run_db_tests();
void run_exif_tests();
void run_cache_tests();
void run_pipeline_tests();

int main() {
    std::cout << "========================================\n";
    std::cout << " Running image-ODB Unit & Integration Tests\n";
    std::cout << "========================================\n";

    int failures = 0;
    auto execute_test = [&](const std::string& name, auto test_fn) {
        std::cout << "[RUNNING] " << name << " ... ";
        try {
            test_fn();
            std::cout << "PASSED\n";
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << "\n";
            failures++;
        } catch (...) {
            std::cout << "FAILED (Unknown exception)\n";
            failures++;
        }
    };

    execute_test("Logger Configuration Tests", run_logger_tests);
    execute_test("pHash Tests", run_phash_tests);
    execute_test("ThumbHash Tests", run_thumbhash_tests);
    execute_test("JPEG & AVIF Codec Tests", run_avif_codec_tests);
    execute_test("Database Schema & CRUD Tests", run_db_tests);
    execute_test("EXIF Parsing Tests", run_exif_tests);
    execute_test("Cache & Memory Tests", run_cache_tests);
    execute_test("Engine & Pipeline Ingestion Tests", run_pipeline_tests);

    std::cout << "========================================\n";
    if (failures == 0) {
        std::cout << " ALL TESTS PASSED!\n";
        return 0;
    } else {
        std::cout << " " << failures << " TEST SUITES FAILED!\n";
        return 1;
    }
}
