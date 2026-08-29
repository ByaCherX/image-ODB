#pragma once

#include "image_odb/core/types.h"
#include <filesystem>
#include <vector>
#include <optional>
#include <memory>

namespace SQLite {
    class Database;
}

namespace image_odb::db {

/**
 * @brief SQLite database manager for metadata storage, burst indexing, and search queries.
 */
class Database {
public:
    explicit Database(const std::filesystem::path& db_path);
    ~Database();

    // Move-only semantics
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /**
     * @brief Initialize tables and indices if they do not exist.
     */
    void initialize_schema();

    /**
     * @brief Insert a new photo record into the database.
     * @param photo The photo object to insert (will have its ID set on success).
     * @return Generated photo ID (> 0) on success, -1 on failure.
     */
    int64_t insert_photo(Photo& photo);

    /**
     * @brief Update an existing photo record in the database.
     * @param photo The photo object with updated values.
     * @return True if record was updated.
     */
    bool update_photo(const Photo& photo);

    /**
     * @brief Insert multiple photo records in a single high-performance transaction.
     * @param photos List of photo objects to insert.
     * @return Number of successfully inserted photos.
     */
    uint64_t insert_photos_batch(std::vector<Photo>& photos);

    /**
     * @brief Insert a burst frame sub-record associated with a photo.
     * @param frame The burst frame to insert.
     * @return Generated frame ID (> 0) on success, -1 on failure.
     */
    int64_t insert_burst_frame(BurstFrame& frame);

    /**
     * @brief Find a photo record by its unique file path with full entity hydration.
     * @param path The file path to search for.
     * @return Optional Photo object if found.
     */
    std::optional<Photo> find_by_path(const std::filesystem::path& path);

    /**
     * @brief Find a photo record by its primary ID with full entity hydration.
     * @param id The photo ID.
     * @return Optional Photo object if found.
     */
    std::optional<Photo> find_by_id(int64_t id);

    /**
     * @brief Find a photo record by its cryptographic content hash with full entity hydration.
     * @param hash Content hash.
     * @return Optional Photo object if found.
     */
    std::optional<Photo> find_by_hash(const std::string& hash);

    /**
     * @brief Query photos matching the given filter and pagination criteria.
     * @param options Query and filter options.
     * @return List of matching Photo records.
     */
    std::vector<Photo> list_photos(const ListOptions& options);

    /**
     * @brief Retrieve all burst frames belonging to a photo container.
     * @param photo_id Primary ID of the container photo.
     * @return List of burst frames.
     */
    std::vector<BurstFrame> get_burst_frames(int64_t photo_id);

    /**
     * @brief Delete a photo and its associated burst frames (via ON DELETE CASCADE).
     * @param photo_id Photo ID to delete.
     * @return True if deleted successfully.
     */
    bool delete_photo(int64_t photo_id);

    /**
     * @brief Check if a photo with the given content hash already exists in the database.
     * @param hash Content hash.
     * @return True if exists.
     */
    bool exists_by_hash(const std::string& hash);

    /**
     * @brief Check if a photo with the given file path already exists in the database.
     * @param path File path.
     * @return True if exists.
     */
    bool exists_by_path(const std::filesystem::path& path);

    /**
     * @brief Return the total number of photo records in the database.
     */
    uint64_t count_photos();

    /**
     * @brief Return the total number of multi-frame burst group containers.
     */
    uint64_t count_burst_groups();

private:
    std::filesystem::path db_path_;
    std::unique_ptr<SQLite::Database> db_;
};

} // namespace image_odb::db
