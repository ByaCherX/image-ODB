#include "image_odb/similarity_engine.h"
#include "image_odb/phash.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace image_odb::detector {

std::vector<BurstCandidateGroup> SimilarityEngine::find_burst_groups(
    const std::vector<Photo>& photos,
    uint32_t max_time_delta_seconds,
    uint32_t max_hamming_distance) {
    
    spdlog::debug("SimilarityEngine: Analyzing {} photo(s) for burst grouping (time_window={}s, max_phash_dist={})",
                  photos.size(), max_time_delta_seconds, max_hamming_distance);

    std::vector<BurstCandidateGroup> groups;
    if (photos.size() < 2) return groups;

    // Create a copy sorted chronologically
    std::vector<Photo> sorted_photos = photos;
    std::sort(sorted_photos.begin(), sorted_photos.end(), [](const Photo& a, const Photo& b) {
        if (a.capture_date.has_value() && b.capture_date.has_value()) {
            return *a.capture_date < *b.capture_date;
        }
        return a.created_at < b.created_at;
    });

    std::vector<bool> visited(sorted_photos.size(), false);

    for (size_t i = 0; i < sorted_photos.size(); ++i) {
        if (visited[i]) continue;

        BurstCandidateGroup current_group;
        current_group.photos.push_back(sorted_photos[i]);
        visited[i] = true;

        for (size_t j = i + 1; j < sorted_photos.size(); ++j) {
            if (visited[j]) continue;

            const auto& prev = current_group.photos.back();
            const auto& next = sorted_photos[j];

            // Check time delta
            bool time_ok = false;
            if (prev.capture_date.has_value() && next.capture_date.has_value()) {
                auto diff_sec = std::chrono::duration_cast<std::chrono::seconds>(*next.capture_date - *prev.capture_date).count();
                time_ok = (std::abs(diff_sec) <= static_cast<int64_t>(max_time_delta_seconds));
            } else {
                auto diff_sec = std::chrono::duration_cast<std::chrono::seconds>(next.created_at - prev.created_at).count();
                time_ok = (std::abs(diff_sec) <= static_cast<int64_t>(max_time_delta_seconds));
            }

            if (!time_ok) {
                // Since photos are sorted by time, further photos will exceed time delta
                break;
            }

            // Check visual similarity via pHash
            uint32_t dist = hash::PHash::hamming_distance(prev.phash, next.phash);
            if (dist <= max_hamming_distance) {
                spdlog::debug("SimilarityEngine: Grouping photo '{}' with burst (pHash dist={})",
                              next.file_path.filename().string(), dist);
                current_group.photos.push_back(next);
                visited[j] = true;
            }
        }

        if (current_group.photos.size() >= 2) {
            spdlog::debug("SimilarityEngine: Formed burst group with {} frames", current_group.photos.size());
            groups.push_back(std::move(current_group));
        }
    }

    spdlog::debug("SimilarityEngine: Total burst groups found: {}", groups.size());
    return groups;
}

} // namespace image_odb::detector
