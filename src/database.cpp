#include "image_odb/database.h"
#include "image_odb/exif_reader.h"
#include <SQLiteCpp/SQLiteCpp.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <set>

namespace image_odb::db {

namespace {

const std::string SELECT_ALL_COLUMNS = R"(
    SELECT 
        id, file_path, file_size, hash, width, height, orientation, mime_type,
        capture_date, latitude, longitude, altitude, location,
        camera_make, camera_model, lens_model, focal_length_mm, focal_length_in_35mm,
        f_number, exposure_time, iso_speed, exposure_bias, flash_fired,
        phash, thumbhash, is_burst_group, frame_count, exif_json, created_at
    FROM photos
)";

/*
 * =====================================================================================
 * NAMED BINDING & HYDRATION ARCHITECTURE
 * =====================================================================================
 * Why Named Columns and Named Parameters (Named Binding) are used:
 * 1. Resilience & Safety: Positional indices (e.g., `getColumn(0)`, `bind(1, ...)`) are
 *    fragile. If columns in SQL queries are reordered, inserted, or removed, positional
 *    bindings silently map data to the wrong variables or crash at runtime.
 * 2. Self-Documenting: Parameter and column names in C++ code match the SQL schema directly.
 * 3. Type-Safe std::optional Handling: Helper templates (`get_optional_column`,
 *    `bind_optional_param`) safely convert SQL NULLs to std::nullopt without boilerplate.
 *
 * HOW TO ADD A NEW COLUMN TO THE `photos` TABLE:
 * -------------------------------------------------------------------------------------
 * When introducing a new column (for example, `rating INTEGER DEFAULT 0`):
 *   1. Schema Migration:
 *      Add the column definition to the `CREATE TABLE photos` query inside
 *      `Database::initialize_schema()`.
 *   2. Data Model:
 *      Add the corresponding field to the `Photo` struct (or relevant nested struct)
 *      in `include/image_odb/core/types.h`.
 *   3. Select Query:
 *      Add the column name to `SELECT_ALL_COLUMNS` in this file.
 *   4. Entity Hydration:
 *      In `hydrate_photo()`, map the column using `query.getColumn("rating")` or
 *      `get_optional_column<int>(query, "rating")`.
 *   5. Parameter Binding:
 *      In `bind_photo_params()`, bind the field using `query.bind(":rating", photo.rating)`
 *      or `bind_optional_param(query, ":rating", photo.rating)`.
 *   6. INSERT / UPDATE Queries:
 *      In `insert_photo()`, `update_photo()`, and `insert_photos_batch()`, add the column
 *      and its `:rating` placeholder to the SQL statement strings.
 * =====================================================================================
 */

/**
 * @brief Utility template to read std::optional values from a named SQLite column.
 *
 * Checks if the specified column is NULL; if so, returns std::nullopt.
 * Otherwise, retrieves the typed value from SQLite.
 */
template <typename T>
std::optional<T> get_optional_column(const SQLite::Statement& query, const char* col_name) {
    const auto col = query.getColumn(col_name);
    if (col.isNull()) {
        return std::nullopt;
    }
    if constexpr (std::is_same_v<T, double>) {
        return col.getDouble();
    } else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint16_t>) {
        return static_cast<T>(col.getUInt());
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return col.getInt64();
    } else if constexpr (std::is_same_v<T, std::string>) {
        return col.getString();
    } else {
        static_assert(!sizeof(T), "Unsupported type for get_optional_column");
    }
}

/**
 * @brief Utility template to bind std::optional values to a named SQLite parameter.
 *
 * If the optional contains a value, binds the underlying value to the specified parameter name.
 * If the optional is empty (std::nullopt), binds a SQL NULL.
 */
template <typename T>
void bind_optional_param(SQLite::Statement& query, const char* param_name, const std::optional<T>& opt) {
    if (opt.has_value()) {
        if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, uint16_t>) {
            query.bind(param_name, static_cast<int>(*opt));
        } else {
            query.bind(param_name, *opt);
        }
    } else {
        query.bind(param_name); // Binds NULL
    }
}

Photo hydrate_photo(SQLite::Statement& query) {
    Photo p;
    p.id                     = query.getColumn("id").getInt64();
    p.file_path              = query.getColumn("file_path").getString();
    p.file_size              = query.getColumn("file_size").getInt64();
    p.hash                   = query.getColumn("hash").getString();
    p.dimensions.width       = query.getColumn("width").getUInt();
    p.dimensions.height      = query.getColumn("height").getUInt();
    p.dimensions.orientation = static_cast<uint16_t>(query.getColumn("orientation").getUInt());
    p.mime_type              = query.getColumn("mime_type").getString();

    const auto capture_col = query.getColumn("capture_date");
    if (!capture_col.isNull()) {
        p.capture_date = metadata::ExifReader::parse_exif_date(capture_col.getString());
    }

    p.location.latitude      = get_optional_column<double>(query, "latitude");
    p.location.longitude     = get_optional_column<double>(query, "longitude");
    p.location.altitude      = get_optional_column<double>(query, "altitude");
    p.location.place_name    = query.getColumn("location").getString();

    p.camera.make            = query.getColumn("camera_make").getString();
    p.camera.model           = query.getColumn("camera_model").getString();
    p.lens.model             = query.getColumn("lens_model").getString();
    p.lens.focal_length_mm   = get_optional_column<double>(query, "focal_length_mm");
    p.lens.focal_length_in_35mm = get_optional_column<double>(query, "focal_length_in_35mm");

    p.exposure.f_number      = get_optional_column<double>(query, "f_number");
    p.exposure.exposure_time_str = query.getColumn("exposure_time").getString();
    p.exposure.iso_speed     = get_optional_column<uint32_t>(query, "iso_speed");
    p.exposure.exposure_bias = get_optional_column<double>(query, "exposure_bias");
    p.exposure.flash_fired   = (query.getColumn("flash_fired").getInt() != 0);

    p.phash                  = static_cast<uint64_t>(query.getColumn("phash").getInt64());
    p.thumbhash              = query.getColumn("thumbhash").getString();
    p.is_burst_group         = (query.getColumn("is_burst_group").getInt() != 0);
    p.frame_count            = query.getColumn("frame_count").getUInt();

    const auto exif_col = query.getColumn("exif_json");
    if (!exif_col.isNull()) {
        try {
            p.exif_json = nlohmann::json::parse(exif_col.getString());
        } catch (...) {
            p.exif_json = nlohmann::json::object();
        }
    }

    const auto created_col = query.getColumn("created_at");
    if (!created_col.isNull()) {
        auto ct = metadata::ExifReader::parse_exif_date(created_col.getString());
        if (ct.has_value()) p.created_at = *ct;
    }

    return p;
}

void bind_photo_params(SQLite::Statement& query, const Photo& photo) {
    query.bind(":file_path", photo.file_path.generic_string());
    query.bind(":file_size", static_cast<int64_t>(photo.file_size));
    query.bind(":hash", photo.hash);
    query.bind(":width", photo.dimensions.width);
    query.bind(":height", photo.dimensions.height);
    query.bind(":orientation", photo.dimensions.orientation);
    query.bind(":mime_type", photo.mime_type);

    if (photo.capture_date.has_value()) {
        query.bind(":capture_date", metadata::ExifReader::format_iso8601(*photo.capture_date));
    } else {
        query.bind(":capture_date");
    }

    bind_optional_param(query, ":latitude", photo.location.latitude);
    bind_optional_param(query, ":longitude", photo.location.longitude);
    bind_optional_param(query, ":altitude", photo.location.altitude);
    query.bind(":location", photo.location.place_name);

    query.bind(":camera_make", photo.camera.make);
    query.bind(":camera_model", photo.camera.model);
    query.bind(":lens_model", photo.lens.model);
    bind_optional_param(query, ":focal_length_mm", photo.lens.focal_length_mm);
    bind_optional_param(query, ":focal_length_in_35mm", photo.lens.focal_length_in_35mm);

    bind_optional_param(query, ":f_number", photo.exposure.f_number);
    query.bind(":exposure_time", photo.exposure.exposure_time_str);
    bind_optional_param(query, ":iso_speed", photo.exposure.iso_speed);
    bind_optional_param(query, ":exposure_bias", photo.exposure.exposure_bias);
    query.bind(":flash_fired", photo.exposure.flash_fired ? 1 : 0);

    query.bind(":phash", static_cast<int64_t>(photo.phash));
    query.bind(":thumbhash", photo.thumbhash);
    query.bind(":is_burst_group", photo.is_burst_group ? 1 : 0);
    query.bind(":frame_count", static_cast<int>(photo.frame_count));
    query.bind(":exif_json", photo.exif_json.dump());
}

} // namespace

Database::Database(const std::filesystem::path& db_path)
    : db_path_(db_path) {
    if (auto parent = db_path.parent_path(); !parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
    }
    spdlog::debug("Database: Opening SQLite database at '{}'", db_path_.string());
    db_ = std::make_unique<SQLite::Database>(
        db_path_.string(),
        SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE
    );
    // Enable Foreign Key constraints
    db_->exec("PRAGMA foreign_keys = ON;");
}

Database::~Database() = default;

Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

void Database::initialize_schema() {
    SQLite::Transaction transaction(*db_);

    db_->exec(R"(
        CREATE TABLE IF NOT EXISTS photos (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path       TEXT NOT NULL UNIQUE,
            file_size       INTEGER NOT NULL,
            hash            TEXT NOT NULL,
            width           INTEGER,
            height          INTEGER,
            orientation     INTEGER DEFAULT 1,
            mime_type       TEXT,
            capture_date    TEXT,
            latitude        REAL,
            longitude       REAL,
            altitude        REAL,
            location        TEXT,
            camera_make     TEXT,
            camera_model    TEXT,
            lens_model      TEXT,
            focal_length_mm REAL,
            focal_length_in_35mm REAL,
            f_number        REAL,
            exposure_time   TEXT,
            iso_speed       INTEGER,
            exposure_bias   REAL,
            flash_fired     BOOLEAN DEFAULT 0,
            phash           INTEGER,
            thumbhash       TEXT,
            is_burst_group  BOOLEAN DEFAULT 0,
            frame_count     INTEGER DEFAULT 1,
            exif_json       TEXT,
            created_at      TEXT DEFAULT CURRENT_TIMESTAMP
        );
    )");

    db_->exec(R"(
        CREATE TABLE IF NOT EXISTS burst_frames (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            photo_id        INTEGER NOT NULL,
            frame_index     INTEGER NOT NULL,
            original_file_name TEXT,
            width           INTEGER,
            height          INTEGER,
            phash           INTEGER,
            thumbhash       TEXT,
            exif_json       TEXT,
            FOREIGN KEY(photo_id) REFERENCES photos(id) ON DELETE CASCADE
        );
    )");

    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_capture_date ON photos(capture_date);");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_camera ON photos(camera_make, camera_model);");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_lens ON photos(lens_model);");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_location ON photos(location);");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_phash ON photos(phash);");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_gps ON photos(latitude, longitude);");
    db_->exec("CREATE INDEX IF NOT EXISTS idx_photos_hash ON photos(hash);");

    transaction.commit();
    spdlog::debug("Database: Schema initialized successfully");
}

int64_t Database::insert_photo(Photo& photo) {
    try {
        SQLite::Statement query(*db_, R"(
            INSERT INTO photos (
                file_path,    file_size,     hash, width,    height,          orientation,         mime_type,
                capture_date, latitude,      longitude,      altitude,        location,
                camera_make,  camera_model,  lens_model,     focal_length_mm, focal_length_in_35mm,
                f_number,     exposure_time, iso_speed,      exposure_bias,   flash_fired,
                phash,        thumbhash,     is_burst_group, frame_count,     exif_json
            ) VALUES (
                :file_path,    :file_size,     :hash,           :width, :height,  :orientation,    :mime_type,
                :capture_date, :latitude,      :longitude,      :altitude,        :location,
                :camera_make,  :camera_model,  :lens_model,     :focal_length_mm, :focal_length_in_35mm,
                :f_number,     :exposure_time, :iso_speed,      :exposure_bias,   :flash_fired,
                :phash,        :thumbhash,     :is_burst_group, :frame_count,     :exif_json
            )
        )");

        bind_photo_params(query, photo);
        query.exec();
        photo.id = db_->getLastInsertRowid();
        spdlog::debug("Database: Inserted photo id={} ('{}', hash: {})", photo.id, photo.file_path.string(), photo.hash);
        return photo.id;
    } catch (const SQLite::Exception& ex) {
        spdlog::error("Failed to insert photo {}: {}", photo.file_path.string(), ex.what());
        return -1;
    }
}

bool Database::update_photo(const Photo& photo) {
    if (photo.id <= 0) return false;
    try {
        SQLite::Statement query(*db_, R"(
            UPDATE photos SET
                file_path = :file_path, file_size = :file_size, hash = :hash, width = :width, height = :height,
                orientation = :orientation, mime_type = :mime_type, capture_date = :capture_date,
                latitude = :latitude, longitude = :longitude, altitude = :altitude, location = :location,
                camera_make = :camera_make, camera_model = :camera_model, lens_model = :lens_model,
                focal_length_mm = :focal_length_mm, focal_length_in_35mm = :focal_length_in_35mm,
                f_number = :f_number, exposure_time = :exposure_time, iso_speed = :iso_speed,
                exposure_bias = :exposure_bias, flash_fired = :flash_fired, phash = :phash,
                thumbhash = :thumbhash, is_burst_group = :is_burst_group, frame_count = :frame_count,
                exif_json = :exif_json
            WHERE id = :id
        )");

        bind_photo_params(query, photo);
        query.bind(":id", static_cast<int64_t>(photo.id));
        query.exec();
        spdlog::debug("Database: Updated photo id={}", photo.id);
        return true;
    } catch (const SQLite::Exception& ex) {
        spdlog::error("Failed to update photo id {}: {}", photo.id, ex.what());
        return false;
    }
}

uint64_t Database::insert_photos_batch(std::vector<Photo>& photos) {
    if (photos.empty()) return 0;

    SQLite::Transaction transaction(*db_);
    uint64_t count = 0;

    SQLite::Statement query(*db_, R"(
        INSERT INTO photos (
            file_path,    file_size,     hash, width,    height,          orientation,         mime_type,
            capture_date, latitude,      longitude,      altitude,        location,
            camera_make,  camera_model,  lens_model,     focal_length_mm, focal_length_in_35mm,
            f_number,     exposure_time, iso_speed,      exposure_bias,   flash_fired,
            phash,        thumbhash,     is_burst_group, frame_count,     exif_json
        ) VALUES (
            :file_path,    :file_size,     :hash,           :width, :height,  :orientation,    :mime_type,
            :capture_date, :latitude,      :longitude,      :altitude,        :location,
            :camera_make,  :camera_model,  :lens_model,     :focal_length_mm, :focal_length_in_35mm,
            :f_number,     :exposure_time, :iso_speed,      :exposure_bias,   :flash_fired,
            :phash,        :thumbhash,     :is_burst_group, :frame_count,     :exif_json
        )
    )");

    for (auto& photo : photos) {
        query.reset();
        bind_photo_params(query, photo);
        query.exec();
        photo.id = db_->getLastInsertRowid();
        count++;
    }

    transaction.commit();
    spdlog::debug("Database: Batch inserted {} photos", count);
    return count;
}

int64_t Database::insert_burst_frame(BurstFrame& frame) {
    try {
        SQLite::Statement query(*db_, R"(
            INSERT INTO burst_frames (
                photo_id, frame_index, original_file_name, width, height, phash, thumbhash, exif_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        )");

        query.bind(1, static_cast<int64_t>(frame.photo_id));
        query.bind(2, static_cast<int>(frame.frame_index));
        query.bind(3, frame.original_file_name);
        query.bind(4, static_cast<int>(frame.width));
        query.bind(5, static_cast<int>(frame.height));
        query.bind(6, static_cast<int64_t>(frame.phash));
        query.bind(7, frame.thumbhash);
        query.bind(8, frame.exif_json.dump());

        query.exec();
        frame.id = db_->getLastInsertRowid();
        spdlog::debug("Database: Inserted burst frame id={} for photo_id={}, index={}",
                      frame.id, frame.photo_id, frame.frame_index);
        return frame.id;
    } catch (const SQLite::Exception& ex) {
        spdlog::error("Failed to insert burst frame: {}", ex.what());
        return -1;
    }
}

std::optional<Photo> Database::find_by_path(const std::filesystem::path& path) {
    try {
        std::string sql = SELECT_ALL_COLUMNS + " WHERE file_path = ? LIMIT 1";
        SQLite::Statement query(*db_, sql);
        query.bind(1, path.generic_string());
        if (query.executeStep()) {
            spdlog::debug("Database: find_by_path found photo for '{}'", path.string());
            return hydrate_photo(query);
        }
    } catch (const SQLite::Exception& ex) {
        spdlog::error("find_by_path error: {}", ex.what());
    }
    return std::nullopt;
}

std::optional<Photo> Database::find_by_id(int64_t id) {
    try {
        std::string sql = SELECT_ALL_COLUMNS + " WHERE id = ? LIMIT 1";
        SQLite::Statement query(*db_, sql);
        query.bind(1, static_cast<int64_t>(id));
        if (query.executeStep()) {
            spdlog::debug("Database: find_by_id found photo id={}", id);
            return hydrate_photo(query);
        }
    } catch (const SQLite::Exception& ex) {
        spdlog::error("find_by_id error: {}", ex.what());
    }
    return std::nullopt;
}

std::optional<Photo> Database::find_by_hash(const std::string& hash) {
    try {
        std::string sql = SELECT_ALL_COLUMNS + " WHERE hash = ? LIMIT 1";
        SQLite::Statement query(*db_, sql);
        query.bind(1, hash);
        if (query.executeStep()) {
            return hydrate_photo(query);
        }
    } catch (const SQLite::Exception& ex) {
        spdlog::error("find_by_hash error: {}", ex.what());
    }
    return std::nullopt;
}

std::vector<Photo> Database::list_photos(const ListOptions& options) {
    std::vector<Photo> results;
    try {
        std::string sql = SELECT_ALL_COLUMNS + " WHERE 1=1";

        if (options.location_filter.has_value()) {
            sql += " AND location LIKE ?";
        }
        if (options.camera_make_filter.has_value()) {
            sql += " AND camera_make = ?";
        }
        if (options.camera_model_filter.has_value()) {
            sql += " AND camera_model = ?";
        }
        if (options.lens_filter.has_value()) {
            sql += " AND lens_model LIKE ?";
        }
        if (options.date_from.has_value()) {
            sql += " AND capture_date >= ?";
        }
        if (options.date_to.has_value()) {
            sql += " AND capture_date <= ?";
        }
        if (options.burst_only.has_value() && *options.burst_only) {
            sql += " AND is_burst_group = 1";
        }
        if (options.min_iso.has_value()) {
            sql += " AND iso_speed >= ?";
        }
        if (options.max_iso.has_value()) {
            sql += " AND iso_speed <= ?";
        }
        if (options.min_focal_length.has_value()) {
            sql += " AND focal_length_mm >= ?";
        }
        if (options.max_focal_length.has_value()) {
            sql += " AND focal_length_mm <= ?";
        }
        if (options.min_f_number.has_value()) {
            sql += " AND f_number >= ?";
        }
        if (options.max_f_number.has_value()) {
            sql += " AND f_number <= ?";
        }

        // Whitelist sort fields to prevent SQL injection
        static const std::set<std::string> valid_sort_fields = {
            "capture_date", "created_at", "file_size", "iso_speed", "f_number", "phash", "id"
        };
        std::string sort_field = "capture_date";
        if (valid_sort_fields.count(options.sort_by)) {
            sort_field = options.sort_by;
        }

        sql += " ORDER BY " + sort_field + (options.ascending ? " ASC" : " DESC");
        sql += " LIMIT ? OFFSET ?";

        SQLite::Statement query(*db_, sql);
        int idx = 1;

        if (options.location_filter.has_value()) query.bind(idx++, "%" + *options.location_filter + "%");
        if (options.camera_make_filter.has_value()) query.bind(idx++, *options.camera_make_filter);
        if (options.camera_model_filter.has_value()) query.bind(idx++, *options.camera_model_filter);
        if (options.lens_filter.has_value()) query.bind(idx++, "%" + *options.lens_filter + "%");
        if (options.date_from.has_value()) query.bind(idx++, metadata::ExifReader::format_iso8601(*options.date_from));
        if (options.date_to.has_value()) query.bind(idx++, metadata::ExifReader::format_iso8601(*options.date_to));
        if (options.min_iso.has_value()) query.bind(idx++, static_cast<int>(*options.min_iso));
        if (options.max_iso.has_value()) query.bind(idx++, static_cast<int>(*options.max_iso));
        if (options.min_focal_length.has_value()) query.bind(idx++, *options.min_focal_length);
        if (options.max_focal_length.has_value()) query.bind(idx++, *options.max_focal_length);
        if (options.min_f_number.has_value()) query.bind(idx++, *options.min_f_number);
        if (options.max_f_number.has_value()) query.bind(idx++, *options.max_f_number);

        query.bind(idx++, static_cast<int>(options.limit));
        query.bind(idx++, static_cast<int>(options.offset));

        while (query.executeStep()) {
            results.push_back(hydrate_photo(query));
        }
        spdlog::debug("Database: list_photos retrieved {} records", results.size());
    } catch (const SQLite::Exception& ex) {
        spdlog::error("list_photos error: {}", ex.what());
    }
    return results;
}

std::vector<BurstFrame> Database::get_burst_frames(int64_t photo_id) {
    std::vector<BurstFrame> frames;
    try {
        SQLite::Statement query(
            *db_, 
            R"(SELECT id, photo_id, frame_index, original_file_name, width, height, phash, thumbhash, exif_json 
               FROM burst_frames 
               WHERE photo_id = ? 
               ORDER BY frame_index ASC
            )"
        );
        query.bind(1, static_cast<int64_t>(photo_id));
        while (query.executeStep()) {
            BurstFrame f;
            f.id                 = query.getColumn(0).getInt64();
            f.photo_id           = query.getColumn(1).getInt64();
            f.frame_index        = query.getColumn(2).getUInt();
            f.original_file_name = query.getColumn(3).getString();
            f.width              = query.getColumn(4).getUInt();
            f.height             = query.getColumn(5).getUInt();
            f.phash              = static_cast<uint64_t>(query.getColumn(6).getInt64());
            f.thumbhash          = query.getColumn(7).getString();
            if (!query.getColumn(8).isNull()) {
                try {
                    f.exif_json = nlohmann::json::parse(query.getColumn(8).getString());
                } catch (...) {
                    f.exif_json = nlohmann::json::object();
                }
            }
            frames.push_back(std::move(f));
        }
        spdlog::debug("Database: get_burst_frames for photo_id={} found {} frames", photo_id, frames.size());
    } catch (const SQLite::Exception& ex) {
        spdlog::error("get_burst_frames error: {}", ex.what());
    }
    return frames;
}

bool Database::delete_photo(int64_t photo_id) {
    try {
        SQLite::Statement query(*db_, "DELETE FROM photos WHERE id = ?");
        query.bind(1, static_cast<int64_t>(photo_id));
        query.exec();
        spdlog::debug("Database: Deleted photo id={}", photo_id);
        return true;
    } catch (const SQLite::Exception& ex) {
        spdlog::error("delete_photo error: {}", ex.what());
        return false;
    }
}

bool Database::exists_by_hash(const std::string& hash) {
    try {
        SQLite::Statement query(*db_, "SELECT 1 FROM photos WHERE hash = ? LIMIT 1");
        query.bind(1, hash);
        return query.executeStep();
    } catch (const SQLite::Exception& ex) {
        spdlog::error("exists_by_hash error: {}", ex.what());
        return false;
    }
}

bool Database::exists_by_path(const std::filesystem::path& path) {
    try {
        SQLite::Statement query(*db_, R"(
            SELECT 1 FROM photos WHERE file_path = ?
            UNION
            SELECT 1 FROM burst_frames WHERE original_file_name = ?
            LIMIT 1
        )");
        query.bind(1, path.generic_string());
        query.bind(2, path.filename().string());
        return query.executeStep();
    } catch (const SQLite::Exception& ex) {
        spdlog::error("exists_by_path error: {}", ex.what());
        return false;
    }
}

uint64_t Database::count_photos() {
    try {
        SQLite::Statement query(*db_, "SELECT COUNT(*) FROM photos");
        if (query.executeStep()) {
            return query.getColumn(0).getUInt();
        }
    } catch (const SQLite::Exception& ex) {
        spdlog::error("count_photos error: {}", ex.what());
    }
    return 0;
}

uint64_t Database::count_burst_groups() {
    try {
        SQLite::Statement query(*db_, "SELECT COUNT(*) FROM photos WHERE is_burst_group = 1");
        if (query.executeStep()) {
            return query.getColumn(0).getUInt();
        }
    } catch (const SQLite::Exception& ex) {
        spdlog::error("count_burst_groups error: {}", ex.what());
    }
    return 0;
}

} // namespace image_odb::db
