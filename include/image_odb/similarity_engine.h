#pragma once

#include "image_odb/core/types.h"
#include <vector>

namespace image_odb::detector {

/**
 * @brief Represents a cluster/group of burst photos candidate for multi-frame AVIF compression.
 */
struct BurstCandidateGroup {
    std::vector<Photo> photos;
};

/**
 * @brief Engine for detecting near-duplicate shots, burst sequences, and visual similarities.
 */
class SimilarityEngine {
public:
    /**
     * @brief Group a list of photos into burst clusters based on timestamp and pHash Hamming distance.
     * @param photos List of photos with computed pHash and capture_date.
     * @param max_time_delta_seconds Maximum time difference between consecutive frames in seconds (default: 3).
     * @param max_hamming_distance Maximum allowable Hamming distance for pHash (default: 5).
     * @return List of candidate burst groups (each containing 2 or more photos).
     */
    static std::vector<BurstCandidateGroup> find_burst_groups(
        const std::vector<Photo>& photos,
        uint32_t max_time_delta_seconds = 3,
        uint32_t max_hamming_distance = 5
    );
};

} // namespace image_odb::detector
