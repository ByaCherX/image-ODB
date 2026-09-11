# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v0.3.0] - 2026-09-11

### 🚀 Highlights & Features

- **Multi-Format Codec Expansion (PNG, WebP, TIFF)**:
  - Added full decoding support for **PNG** using `libspng` with in-memory parsing and conversion tests ([c451775](https://github.com/ByaCherX/image-ODB/commit/c451775)).
  - Added decoding support for **WebP** via `libwebp` ([2da916e](https://github.com/ByaCherX/image-ODB/commit/2da916e)).
  - Added decoding support for **TIFF** images via `libtiff` with custom memory-stream handling (`MemTiffStream`) ([2da916e](https://github.com/ByaCherX/image-ODB/commit/2da916e)).
  - Expanded `SUPPORTED_DECODE_EXTENSIONS` with `.png`, `.webp`, `.tif`, and `.tiff`.
- **Parallel & Accelerated AVIF Processing**:
  - Implemented multi-threaded AVIF encoding with `threads` option in `EncodeOptions` (default `0` = auto system thread count) ([4bdffdb](https://github.com/ByaCherX/image-ODB/commit/4bdffdb)).
  - Added `-t, --threads` CLI parameter to `image` command and forwarded thread configuration to `scan` convert pipeline ([4bdffdb](https://github.com/ByaCherX/image-ODB/commit/4bdffdb)).
  - Explicitly selected `AVIF_CODEC_CHOICE_DAV1D` for high-throughput frame extraction and decoding ([4bdffdb](https://github.com/ByaCherX/image-ODB/commit/4bdffdb)).
- **Codec & Image Processing Performance**:
  - Optimized `resize_aspect_fit` with precomputed horizontal lookup tables (`lut_src_x`) to eliminate O(w×h) floating-point divisions, combined with row/pixel `memcpy` fast paths ([50c0fc0](https://github.com/ByaCherX/image-ODB/commit/50c0fc0)).
  - Added `extract_frame` and `get_frame_count` overloads in `ImageCodec` for multi-frame AVIF containers ([50c0fc0](https://github.com/ByaCherX/image-ODB/commit/50c0fc0)).
  - Optimized file I/O in `read_file_bytes` and refactored `encode_memory` using switch-case logic ([50c0fc0](https://github.com/ByaCherX/image-ODB/commit/50c0fc0)).

### 🔄 Refactoring & Architecture Simplification

- **Removed Cache Subsystem**:
  - Removed `CacheManager`, `LRUCache`, and `DiskCache` implementations to eliminate cache overhead and simplify storage semantics ([4386aac](https://github.com/ByaCherX/image-ODB/commit/4386aac)).
  - Removed `cache` CLI command and updated `preview` to generate previews dynamically on-demand ([4386aac](https://github.com/ByaCherX/image-ODB/commit/4386aac)).
  - Streamlined `Engine` and `Pipeline` by eliminating cache manager dependencies ([4386aac](https://github.com/ByaCherX/image-ODB/commit/4386aac)).

### 📦 Build & Tooling Enhancements

- Added Interprocedural Optimization (IPO / LTO) across project build targets (`CheckIPOSupported`) ([4bdffdb](https://github.com/ByaCherX/image-ODB/commit/4bdffdb)).
- Added compiler and linker Release optimization flags for MSVC (`/O2 /Oi /Gy /Gw`, `/OPT:REF /OPT:ICF`) and GCC/Clang (`-O3 -ffunction-sections -fdata-sections`, `-Wl,--gc-sections`) ([4bdffdb](https://github.com/ByaCherX/image-ODB/commit/4bdffdb)).
- Bumped project version to `0.3.0` across CMake, `vcpkg.json`, and C++ headers (`image_odb.h`) ([4bdffdb](https://github.com/ByaCherX/image-ODB/commit/4bdffdb)).

---

## [v0.2.0] - 2026-09-09

### 🚀 Highlights & Features

- **CLI Utility Suite**:
  - Implemented full CLI toolset with modular command architecture (`init`, `scan`, `list`, `image`, `preview`, `cache`, `extract`) ([29afffd](https://github.com/ByaCherX/image-ODB/commit/29afffd)).
  - Added comprehensive documentation and usage examples for CLI in `cli/README.md`.
- **Image Codecs & Processing**:
  - Centralized image encoding/decoding through unified `ImageCodec` with extensible options ([daed585](https://github.com/ByaCherX/image-ODB/commit/daed585)).
  - Enhanced AVIF codec with in-memory encoding/decoding and EXIF metadata handling ([ec5d51a](https://github.com/ByaCherX/image-ODB/commit/ec5d51a)).
  - Enhanced JPEG encoding/decoding with EXIF data support and in-memory buffer operations ([5353f82](https://github.com/ByaCherX/image-ODB/commit/5353f82)).
  - Added support for burst/multi-frame encoding and decoding ([e4ee926](https://github.com/ByaCherX/image-ODB/commit/e4ee926)).
  - Implemented robust format detection based on byte span magic prefixes ([48a830e](https://github.com/ByaCherX/image-ODB/commit/48a830e), [a1fbcfa](https://github.com/ByaCherX/image-ODB/commit/a1fbcfa)).
- **EXIF & Metadata**:
  - Centralized EXIF date parsing and formatting utilities in `util` namespace ([bf69587](https://github.com/ByaCherX/image-ODB/commit/bf69587)).
  - Added EXIF date extraction fallback from filename and file modification timestamps ([b814976](https://github.com/ByaCherX/image-ODB/commit/b814976)).
  - Refactored EXIF metadata handling with `exif_convert` and expanded unit tests ([33094c8](https://github.com/ByaCherX/image-ODB/commit/33094c8)).
  - Added EXIF data storage directly within `ImageBuffer` ([6a01d8d](https://github.com/ByaCherX/image-ODB/commit/6a01d8d)).
- **Caching & Storage**:
  - Added `CacheMode` configuration to `CacheManager` for customizable caching strategies ([ca9059f](https://github.com/ByaCherX/image-ODB/commit/ca9059f)).
  - Improved SQLite integration, indexing pipeline, and disk cache management.

### 🔄 Refactoring & Code Quality

- Cleaned up debug logging across multiple core files for cleaner console output ([c1e38cc](https://github.com/ByaCherX/image-ODB/commit/c1e38cc)).
- Removed unused header includes across library and test source files ([1a19a5a](https://github.com/ByaCherX/image-ODB/commit/1a19a5a)).
- Simplified default logger formatting pattern by removing redundant timestamps ([bc9bf4e](https://github.com/ByaCherX/image-ODB/commit/bc9bf4e)).
- Removed obsolete file-based JPEG decode/encode functions and deprecated format extension checks ([33657e5](https://github.com/ByaCherX/image-ODB/commit/33657e5), [07d46b9](https://github.com/ByaCherX/image-ODB/commit/07d46b9)).
- Standardized image format representations (`ImageFormat`) ([7523b53](https://github.com/ByaCherX/image-ODB/commit/7523b53)).

### 📦 Build & Project Setup

- Bumped project version to `0.2.0` in CMake and version helper functions ([b110c3e](https://github.com/ByaCherX/image-ODB/commit/b110c3e)).
- Updated CMakeLists.txt for consistent CLI target naming and added project license and documentation ([c3b539e](https://github.com/ByaCherX/image-ODB/commit/c3b539e)).
- Initial project setup with C++20 standard, CMake presets, vcpkg dependencies ([80631ef](https://github.com/ByaCherX/image-ODB/commit/80631ef)).

---

### 📝 Commit History

| Commit | Date | Author | Description |
|---|---|---|---|
| `4bdffdb` | 2026-09-11 | EmreKayal | feat: Add multithreaded AVIF encoding, IPO optimizations, and bump version to v0.3.0 |
| `2da916e` | 2026-09-11 | EmreKayal | feat: Add WebP and TIFF codec support with decoding capabilities |
| `50c0fc0` | 2026-09-10 | EmreKayal | feat: Enhance image codec functionality with new frame extraction and count methods |
| `c451775` | 2026-09-10 | EmreKayal | feat: Add PNG codec support and update dependencies |
| `4386aac` | 2026-09-09 | EmreKayal | Refactor caching system: remove CacheManager, LRU Cache, and Disk Cache |
| `45133f5` | 2026-09-09 | EmreKayal | docs: Add CHANGELOG.md with commit summary for v0.2.0 release |
| `29afffd` | 2026-09-09 | EmreKayal | Add CLI commands for image processing and database management |
| `1a19a5a` | 2026-09-09 | EmreKayal | feat: Remove unused includes from multiple source files for cleaner code |
| `bc9bf4e` | 2026-09-09 | EmreKayal | feat: Simplify default logger pattern by removing timestamp from formatting |
| `bf69587` | 2026-09-08 | EmreKayal | feat: Refactor EXIF date handling by centralizing parsing and formatting functions in util namespace |
| `c1e38cc` | 2026-09-08 | EmreKayal | feat: Remove debug logging statements across multiple files for cleaner output |
| `b110c3e` | 2026-09-08 | EmreKayal | feat: Bump version to 0.2.0 and update related versioning functions |
| `6a01d8d` | 2026-09-07 | EmreKayal | feat: Add EXIF data storage to ImageBuffer and update encoding functions to use centralized options |
| `ec5d51a` | 2026-09-07 | EmreKayal | feat: Refactor AVIF codec to support in-memory encoding/decoding and EXIF metadata handling |
| `5353f82` | 2026-09-07 | EmreKayal | feat: Enhance JPEG encoding/decoding with EXIF data support and refactor encode_memory function |
| `daed585` | 2026-09-06 | EmreKayal | feat: Update image encoding to use centralized ImageCodec with options |
| `e4ee926` | 2026-09-06 | EmreKayal | feat: Enhance image codec with new encoding/decoding functions and support for burst encoding |
| `33657e5` | 2026-09-06 | EmreKayal | refactor: Remove file-based JPEG decode/encode functions and related utility |
| `33094c8` | 2026-09-06 | EmreKayal | feat: Refactor EXIF metadata handling with new exif_convert function and update tests |
| `a1fbcfa` | 2026-09-06 | EmreKayal | feat: Add utility functions to check byte span prefixes |
| `07d46b9` | 2026-09-06 | EmreKayal | refactor: Remove deprecated image format extension checks and related functions |
| `c3b539e` | 2026-09-03 | EmreKayal | feat: Update CMakeLists.txt for consistent CLI naming and add LICENSE and README files |
| `48a830e` | 2026-09-02 | EmreKayal | feat: Implement image format detection, conversion options, and caching enhancements |
| `ca9059f` | 2026-09-02 | EmreKayal | feat: You can now adjust the cache with CacheMode in CacheManager. |
| `7523b53` | 2026-09-01 | EmreKayal | fix: preview_format removed, use ImageFormat |
| `b814976` | 2026-08-31 | EmreKayal | feat: Add EXIF date parsing from filename and file modification date retrieval |
| `80631ef` | 2026-08-29 | EmreKayal | > initial commit |
