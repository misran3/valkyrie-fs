#include "cache_manager.hpp"
#include <algorithm>
#include <stdexcept>

namespace valkyrie {

CacheManager::CacheManager(size_t max_size_bytes)
    : max_size_(max_size_bytes)
    , current_size_(0) {
}

void CacheManager::insert_chunk(const std::string& s3_key, size_t offset,
                                 const std::vector<char>& data, CacheZone zone) {
    evict_if_needed(data.size());

    std::unique_lock<std::shared_mutex> lock(cache_mutex_);

    // Get or create file entry
    auto& file_ptr = files_[s3_key];
    if (!file_ptr) {
        file_ptr = std::make_shared<FileEntry>(s3_key, zone);

        // Track in appropriate zone
        if (zone == CacheZone::HOT) {
            hot_lru_.push_back(s3_key);
        } else {
            prefetch_fifo_.push_back(s3_key);
        }
    }

    // Insert chunk
    {
        std::unique_lock<std::shared_mutex> file_lock(file_ptr->mutex);
        file_ptr->chunks[offset] = Chunk(data);
    }

    current_size_ += data.size();
}

std::optional<Chunk> CacheManager::get_chunk(const std::string& s3_key, size_t offset) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);

    auto it = files_.find(s3_key);
    if (it == files_.end()) {
        return std::nullopt;
    }

    std::shared_lock<std::shared_mutex> file_lock(it->second->mutex);
    auto chunk_it = it->second->chunks.find(offset);
    if (chunk_it == it->second->chunks.end()) {
        return std::nullopt;
    }

    return chunk_it->second;
}

void CacheManager::access(const std::string& s3_key, size_t offset) {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);

    auto it = files_.find(s3_key);
    if (it == files_.end()) return;

    auto& file = it->second;

    // Update chunk access time
    {
        std::unique_lock<std::shared_mutex> file_lock(file->mutex);
        auto chunk_it = file->chunks.find(offset);
        if (chunk_it != file->chunks.end()) {
            chunk_it->second.update_access_time();
        }
    }

    // Promote PREFETCH -> HOT on access
    if (file->zone == CacheZone::PREFETCH) {
        file->zone = CacheZone::HOT;

        // Move from prefetch_fifo to hot_lru
        auto fifo_it = std::find(prefetch_fifo_.begin(), prefetch_fifo_.end(), s3_key);
        if (fifo_it != prefetch_fifo_.end()) {
            prefetch_fifo_.erase(fifo_it);
        }
        hot_lru_.push_back(s3_key);
    }
}

bool CacheManager::contains(const std::string& s3_key) const {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    return files_.find(s3_key) != files_.end();
}

CacheZone CacheManager::get_zone(const std::string& s3_key) const {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);

    auto it = files_.find(s3_key);
    if (it == files_.end()) {
        throw std::runtime_error("File not in cache: " + s3_key);
    }

    return it->second->zone;
}

void CacheManager::promote_to_hot(const std::string& s3_key) {
    access(s3_key, 0);  // Use access logic for promotion
}

CacheManager::Stats CacheManager::get_stats() const {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);

    Stats stats;
    stats.current_size = current_size_;
    stats.max_size = max_size_;
    stats.num_files = files_.size();

    size_t hot_size = 0, prefetch_size = 0, num_chunks = 0;

    for (const auto& [key, file] : files_) {
        std::shared_lock<std::shared_mutex> file_lock(file->mutex);
        size_t file_size = calculate_file_size(*file);
        num_chunks += file->chunks.size();

        if (file->zone == CacheZone::HOT) {
            hot_size += file_size;
        } else {
            prefetch_size += file_size;
        }
    }

    stats.hot_zone_size = hot_size;
    stats.prefetch_zone_size = prefetch_size;
    stats.num_chunks = num_chunks;

    return stats;
}

void CacheManager::evict_if_needed(size_t required_space) {
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);

    while (current_size_ + required_space > max_size_) {
        // Try evicting from prefetch first (less important)
        if (!prefetch_fifo_.empty()) {
            evict_fifo_prefetch();
        } else if (!hot_lru_.empty()) {
            evict_lru_hot();
        } else {
            break;  // Cache empty, can't evict more
        }
    }
}

void CacheManager::evict_lru_hot() {
    if (hot_lru_.empty()) return;

    // Find LRU (oldest access time)
    std::string lru_key;
    uint64_t oldest_time = UINT64_MAX;

    for (const auto& key : hot_lru_) {
        auto it = files_.find(key);
        if (it == files_.end()) continue;

        std::shared_lock<std::shared_mutex> file_lock(it->second->mutex);
        for (const auto& [offset, chunk] : it->second->chunks) {
            if (chunk.last_access_time < oldest_time) {
                oldest_time = chunk.last_access_time;
                lru_key = key;
            }
        }
    }

    if (!lru_key.empty()) {
        auto it = files_.find(lru_key);
        if (it != files_.end()) {
            size_t freed = calculate_file_size(*it->second);
            files_.erase(it);
            current_size_ -= freed;

            auto lru_it = std::find(hot_lru_.begin(), hot_lru_.end(), lru_key);
            if (lru_it != hot_lru_.end()) {
                hot_lru_.erase(lru_it);
            }
        }
    }
}

void CacheManager::evict_fifo_prefetch() {
    if (prefetch_fifo_.empty()) return;

    // Evict first (oldest) entry
    std::string key = prefetch_fifo_.front();
    prefetch_fifo_.erase(prefetch_fifo_.begin());

    auto it = files_.find(key);
    if (it != files_.end()) {
        size_t freed = calculate_file_size(*it->second);
        files_.erase(it);
        current_size_ -= freed;
    }
}

size_t CacheManager::calculate_file_size(const FileEntry& entry) const {
    size_t total = 0;
    for (const auto& [offset, chunk] : entry.chunks) {
        total += chunk.data.size();
    }
    return total;
}

}  // namespace valkyrie
