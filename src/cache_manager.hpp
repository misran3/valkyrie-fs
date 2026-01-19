#pragma once

#include "types.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <chrono>
#include <memory>
#include <future>

namespace valkyrie {

// Single data chunk (4MB typical)
struct Chunk {
    std::vector<char> data;
    std::shared_future<void> ready_future;  // For async loading
    uint64_t last_access_time;  // Microseconds since epoch

    Chunk() : last_access_time(0) {}

    Chunk(std::vector<char> d)
        : data(std::move(d))
        , last_access_time(get_current_time()) {}

    static uint64_t get_current_time() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    void update_access_time() {
        last_access_time = get_current_time();
    }
};

// File entry containing multiple chunks
struct FileEntry {
    std::string s3_key;
    size_t total_size;  // Total file size (if known)
    std::map<size_t, Chunk> chunks;  // offset -> chunk
    CacheZone zone;
    mutable std::shared_mutex mutex;  // Per-file lock

    FileEntry(const std::string& key, CacheZone z)
        : s3_key(key), total_size(0), zone(z) {}
};

// Main cache manager with two-tier architecture
class CacheManager {
public:
    explicit CacheManager(size_t max_size_bytes);

    // Insert chunk into cache
    void insert_chunk(const std::string& s3_key, size_t offset,
                      const std::vector<char>& data, CacheZone zone);

    // Get chunk if exists
    std::optional<Chunk> get_chunk(const std::string& s3_key, size_t offset);

    // Access chunk (updates LRU, may promote zone)
    void access(const std::string& s3_key, size_t offset);

    // Check if file exists in cache
    bool contains(const std::string& s3_key) const;

    // Get file's zone
    CacheZone get_zone(const std::string& s3_key) const;

    // Promote from PREFETCH to HOT
    void promote_to_hot(const std::string& s3_key);

    // Get cache statistics
    struct Stats {
        size_t current_size;
        size_t max_size;
        size_t hot_zone_size;
        size_t prefetch_zone_size;
        size_t num_files;
        size_t num_chunks;
    };
    Stats get_stats() const;

private:
    void evict_if_needed(size_t required_space);
    void evict_lru_hot();
    void evict_fifo_prefetch();
    size_t calculate_file_size(const FileEntry& entry) const;

    size_t max_size_;
    size_t current_size_;

    // File storage: s3_key -> FileEntry
    std::unordered_map<std::string, std::shared_ptr<FileEntry>> files_;
    mutable std::shared_mutex cache_mutex_;  // Global cache lock

    // LRU tracking for HOT zone (ordered by access time)
    std::vector<std::string> hot_lru_;

    // FIFO tracking for PREFETCH zone (insertion order)
    std::vector<std::string> prefetch_fifo_;
};

}  // namespace valkyrie
