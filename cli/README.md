# `image_cli` — Command-Line Interface Guide

`image_cli` is the official high-performance CLI utility for the **image-ODB** photo database and AVIF multi-frame archiving system. It enables fast local indexing, EXIF & optics metadata extraction, perceptual hashing (`pHash`), instant visual placeholders (`ThumbHash`), two-tier caching, and automated burst shot compression into single multi-frame AVIF containers.

---

## 📑 Table of Contents

1. [Quick Start](#-quick-start)
2. [Global Options & First-Time Prompt](#-global-options--first-time-prompt)
3. [Commands Reference](#-commands-reference)
   - [`init` — Initialize Workspace](#1-init--initialize-workspace)
   - [`image` — Standalone Image Encode & Decode](#2-image--standalone-image-encode--decode)
   - [`scan` — Index Photos, Convert & Compress Bursts](#3-scan--index-photos-convert--compress-bursts)
   - [`list` — Query & Filter Photos](#4-list--query--filter-photos)
   - [`extract` — Extract Frame from AVIF](#5-extract--extract-frame-from-avif)
   - [`preview` — Get Thumbnail Preview](#6-preview--get-thumbnail-preview)
   - [`cache` — Inspect & Manage Cache](#7-cache--inspect--manage-cache)
4. [Real-World Usage Scenarios](#-real-world-usage-scenarios)
5. [Performance & Tips](#-performance--tips)

---

## 🚀 Quick Start

```bash
# 1. Initialize a new photo repository
image_cli init -d C:\PhotosDB

# 2. Encode any image to AVIF with embedded thumbnail
image_cli image photo.jpg -o photo.avif --encode -q 85 -s 6 --embed-thumb

# 3. Decode AVIF back to JPEG
image_cli image photo.avif -o photo.jpg --decode -q 90

# 4. Ingest photos from an SD card, convert non-AVIFs to AVIF on-the-fly, and save only disk cache
image_cli scan -d D:\DCIM -w C:\PhotosDB --group-bursts --convert --delete-source --cache disk

# 5. Query all Sony photos with ISO <= 400
image_cli list -d C:\PhotosDB --camera-make Sony --iso-max 400

# 6. View cache disk usage
image_cli cache -d C:\PhotosDB
```

---

## ⚙️ Global Options & First-Time Prompt

| Flag | Description |
| :--- | :--- |
| `-h, --help` | Print help screen and command summaries. |
| `--version` | Display program version information. |
| `--loglevel <level>` | Set logging level (`trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`). Default: `info`. |
| `-v, --verbose` | Enable verbose debug logging (equivalent to `--loglevel=debug`). |

---

## 📖 Commands Reference

### 1. `init` — Initialize Workspace

Creates the SQLite metadata database (`photos.db`), table schemas, foreign key cascade rules, indices, and the `.photo_cache/` directory.

```bash
image_cli init [-d <workspace_dir>]
```

#### Options:
* `-d, --dir <path>`: Target workspace directory (default: current directory `.`).

---

### 2. `image` — Standalone Image Encode & Decode

Provides direct standalone encoding of any image format (`.jpg`, `.jpeg`, `.png`, `.webp`, `.tif`, `.bmp`) to `.avif` or decoding of `.avif` to `.jpg`.

```bash
image_cli image <input> [options]
```

#### Options:
| Flag | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `input` | `path` | *(Required)* | Input image file path. |
| `-o, --output` | `path` | `""` | Destination output file path (auto-generated if omitted). |
| `-e, --encode` | `flag` | `false` | Force encode mode (converts input image to AVIF). |
| `-d, --decode` | `flag` | `false` | Force decode mode (decodes AVIF to JPEG). |
| `-q, --quality` | `int` | `80` | Compression quality factor ($1\text{--}100$). |
| `-s, --speed` | `int` | `6` | AVIF CPU encoder speed effort ($0\text{--}10$). |
| `--lossless` | `flag` | `false` | Enable lossless AVIF compression. |
| `--subsampling` | `string` | `420` | Chroma subsampling (`420`, `422`, `444`, `400`). |
| `--depth, --bit-depth` | `int` | `8` | Bit depth per channel (`8`, `10`, `12`). |
| `--embed-thumb` | `flag` | `false` | Generate and attach downscaled preview thumbnail inside AVIF container. |

#### Examples:
```bash
# High-quality AVIF encode with embedded thumbnail
image_cli image photo.jpg -o photo.avif --encode -q 85 -s 6 --embed-thumb

# Lossless 10-bit AVIF encode with 4:4:4 chroma subsampling
image_cli image graphic.png -o graphic.avif -e --lossless --subsampling 444 --depth 10

# Decode AVIF to JPEG
image_cli image sequence.avif -o frame.jpg --decode -q 92
```

---

### 3. `scan` — Index Photos, Convert & Compress Bursts

Traverses the input directory, computes streaming BLAKE3 cryptographic hashes, extracts EXIF / camera / lens / exposure metadata (with filename fallback parsing), computes 64-bit DCT `pHash` and ~25-byte `ThumbHash`, clusters similar burst shots, optionally converts to AVIF on-the-fly, and persists records.

```bash
image_cli scan [options]
```

#### Options:
| Flag | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `-d, --dir` | `path` | `.` | Directory containing photos to scan. |
| `-w, --workspace` | `path` | `.` | Workspace directory for database and cache (default: same as `-d`). |
| `--group-bursts` | `flag` | `false` | Automatically detect and compress burst sequences into `.avif` with I-Frame + P-Frames. |
| `--format` | `string` | `avif` | Thumbnail preview format (`avif` or `jpeg`). |
| `-t, --threads` | `int` | `0` (Auto) | Number of worker threads for parallel ingestion (`0` = all CPU cores). |
| `--no-recursive` | `flag` | `false` | Only scan top-level folder without descending into subdirectories. |
| `--no-previews` | `flag` | `false` | Skip pre-generating thumbnail previews (speeds up initial raw scan). |
| `--burst-time` | `int` | `3` | Maximum time window in seconds between consecutive burst frames. |
| `--burst-dist` | `int` | `5` | Maximum allowable pHash Hamming distance for visual similarity clustering. |
| `--convert` | `flag` | `false` | Automatically convert all non-AVIF images to AVIF format during ingestion. |
| `--convert-quality` | `int` | `80` | AVIF conversion quality ($1\text{--}100$). |
| `--convert-speed` | `int` | `6` | AVIF conversion speed ($0\text{--}10$). |
| `--convert-lossless` | `flag` | `false` | Enable lossless AVIF conversion during scan. |
| `--convert-subsampling` | `string` | `420` | Conversion chroma subsampling (`420`, `422`, `444`, `400`). |
| `--convert-depth` | `int` | `8` | Conversion bit depth (`8`, `10`, `12`). |
| `--convert-out-dir` | `path` | `""` | Optional destination directory for converted AVIFs. |
| `--delete-source` | `flag` | `false` | Delete original source files after successful AVIF conversion. |
| `--cache` | `string` | `all` | Cache mode (`all`, `disk`, `ram`, `none`). |
| `--embed-thumb` | `flag` | `false` | Embed downscaled thumbnail inside converted AVIF. |

#### Examples:
```bash
# Ingest with on-the-fly AVIF conversion, source cleanup, and disk-only cache
image_cli scan -d D:\DCIM -w C:\PhotosDB --convert --convert-quality 85 --delete-source --cache disk

# High-speed parallel scan with burst clustering
image_cli scan -d E:\SportsEvents -w C:\PhotosDB --group-bursts --burst-time 5 --burst-dist 3 -t 16
```

---

### 4. `list` — Query & Filter Photos

Queries the database with rich filters and sorting options, presenting results as a formatted tabular ASCII view or a structured JSON array.

```bash
image_cli list [options]
```

#### Options:
| Flag | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `-d, --dir` | `path` | `.` | Workspace directory containing `photos.db`. |
| `--camera-make` | `string` | `""` | Filter by manufacturer (e.g. `Sony`, `Canon`, `Apple`, `Nikon`). |
| `--camera-model`| `string` | `""` | Filter by model (e.g. `ILCE-7M4`, `EOS R5`, `iPhone 15 Pro`). |
| `--lens` | `string` | `""` | Filter by lens substring (e.g. `24-70mm`, `F2.8`, `70-200mm`). |
| `--location` | `string` | `""` | Substring match on location name / city. |
| `--from` | `date` | `""` | Start date filter (`YYYY:MM:DD HH:MM:SS` or ISO8601). |
| `--to` | `date` | `""` | End date filter. |
| `--iso-min` | `int` | `0` | Minimum ISO speed rating. |
| `--iso-max` | `int` | `0` | Maximum ISO speed rating. |
| `--burst-only` | `flag` | `false` | List only multi-frame burst containers. |
| `--sort` | `string` | `capture_date` | Sort field: `capture_date`, `created_at`, `file_size`, `iso_speed`, `f_number`. |
| `--asc` | `flag` | `false` | Sort in ascending order (default: descending / newest first). |
| `--limit` | `int` | `50` | Maximum number of records to return. |
| `--offset` | `int` | `0` | Pagination offset. |
| `--json` | `flag` | `false` | Output results in JSON format. |

#### Examples:

**Tabular ASCII View:**
```bash
image_cli list -d C:\PhotosDB --camera-make Sony --lens "24-70mm" --limit 10
```
*Output:*
```text
ID    Size        Frames    Camera            Capture Date          Path
------------------------------------------------------------------------------------------
1     3417 KB     1         Sony ILCE-7M4     2026-05-10T14:20:00Z  photos/tokyo_01.jpg
4     1464 KB     5 (Burst) Sony ILCE-7M4     2026-05-10T14:25:30Z  bursts/burst_9a8b7c.avif
Total displayed: 2 records
```

**JSON Output (Ideal for Web APIs, Python Scripts, or CLI pipelines):**
```bash
image_cli list -d C:\PhotosDB --burst-only --json
```
*Output:*
```json
[
  {
    "id": 4,
    "file_path": "bursts/burst_9a8b7c.avif",
    "file_size": 1500420,
    "hash": "9a8b7c6d5e4f3a2b1c0d...",
    "width": 6000,
    "height": 4000,
    "mime_type": "image/avif",
    "is_burst_group": true,
    "frame_count": 5,
    "thumbhash": "3OcRJYB4d3h/iIeHeEh3eIeH",
    "phash": 1311768467463790320,
    "capture_date": "2026-05-10T14:25:30Z",
    "camera": {
      "make": "Sony",
      "model": "ILCE-7M4"
    },
    "lens": {
      "model": "FE 24-70mm F2.8 GM II",
      "focal_length_mm": 50.0
    },
    "exposure": {
      "f_number": 2.8,
      "exposure_time": "1/1000",
      "iso_speed": 100
    }
  }
]
```

---

### 5. `extract` — Extract Frame from AVIF

Extracts any specific single frame from a multi-frame AVIF burst sequence and writes it to an independent image file on disk.

```bash
image_cli extract <avif_file> -f <frame_index> -o <output_file>
```

#### Options:
* `avif_file` *(Required)*: Path to input multi-frame `.avif` container.
* `-f, --frame <int>`: Zero-based frame index to extract (default: `0`).
* `-o, --output <path>` *(Required)*: Output destination image path (e.g. `frame_02.jpg` or `frame_02.avif`).

#### Example:
```bash
image_cli extract C:\PhotosDB\bursts\burst_action.avif -f 2 -o C:\Exports\best_shot.jpg
```

---

### 6. `preview` — Get Thumbnail Preview

Fetches the visual preview for any photo by ID. Resolves in order: **RAM LRU Cache $\to$ Disk Cache $\to$ On-the-fly synthesis**.

```bash
image_cli preview <photo_id> [options]
```

#### Options:
* `id` *(Required)*: Database photo ID.
* `-d, --dir <path>`: Workspace directory (default: `.`).
* `-o, --output <path>`: Optional destination file path to save the thumbnail preview.
* `--format <avif|jpeg>`: Desired preview format (default: `avif`).
* `--cache <all|disk|ram|none>`: Cache mode to honor during preview retrieval (default: `all`).

#### Example:
```bash
image_cli preview 42 -d C:\PhotosDB -o thumb_42.jpg --format jpeg --cache disk
```

---

### 7. `cache` — Inspect & Manage Cache

Displays cache storage statistics or purges cached files.

```bash
image_cli cache [options]
```

#### Options:
* `-d, --dir <path>`: Workspace directory (default: `.`).
* `--clear`: Purge cached preview files.
* `--memory-only`: Clear only in-memory RAM cache without deleting files from disk.

#### Examples:
```bash
# View cache usage statistics
image_cli cache -d C:\PhotosDB

# Output:
# Cache Location: C:\PhotosDB\.photo_cache
# Cached Previews: 1,420 files
# Total Disk Usage: 48,290 KB (47 MB)

# Clear disk and RAM cache
image_cli cache -d C:\PhotosDB --clear
```

---

## 💡 Real-World Usage Scenarios

### Scenario A: Ingesting Camera SD Card with Extreme Compression
A photographer shoots 200 high-speed burst frames during a sporting match:
```bash
# Ingest with burst clustering enabled
image_cli scan -d D:\DCIM\100MSDCF -w C:\PhotoArchive --group-bursts -t 12
```
*Result:* Loose JPEG/AVIF frames are clustered into 10 multi-frame AVIF containers. Disk space consumption drops from ~2.4 GB down to ~350 MB ($\sim 85\%$ storage reduction) while preserving full visual fidelity and individual frame extraction.

### Scenario B: Finding Low-Light Night Shots
```bash
image_cli list -d C:\PhotoArchive --iso-min 3200 --sort iso_speed
```

### Scenario C: Filtering Photos Taken with a Specific Prime Lens
```bash
image_cli list -d C:\PhotoArchive --lens "85mm F1.4" --from "2026:01:01 00:00:00"
```

### Scenario D: Exporting Data for Machine Learning or Web Gallery
```bash
image_cli list -d C:\PhotoArchive --camera-make "Apple" --limit 500 --json > apple_photos.json
```

---

## ⚡ Performance & Optimization Tips

1. **Multithreading:** By default (`-t 0`), `image_cli scan` utilizes all available CPU logical cores via a worker queue. On modern 8+ core CPUs, scanning speeds exceed **200+ photos/second**.
2. **Preview Pre-generation:** Use `--format avif` for thumbnail previews to achieve $30-50\%$ smaller thumbnail files compared to JPEG at equivalent visual quality.
3. **Deduplication:** Repeatedly scanning the same directory will skip unchanged files automatically via streaming SHA-256 and path indexing without re-decoding images.
4. **SQLite WAL Mode:** The database automatically benefits from composite multi-column indexing for sub-millisecond query responses across $100,000+$ photos.
