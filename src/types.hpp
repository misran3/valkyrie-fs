#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace valkyrie {

// Cache zone classification
enum class CacheZone {
    HOT,        // Recently accessed, LRU eviction
    PREFETCH    // Predicted future access, FIFO eviction
};

// Prefetch task priority
enum class Priority {
    URGENT,      // On-demand miss, user waiting
    NORMAL,      // Predicted next file
    BACKGROUND   // Lookahead (N+2, N+3, ...)
};

// Constants
constexpr size_t DEFAULT_CHUNK_SIZE = 4 * 1024 * 1024;  // 4MB
constexpr size_t DEFAULT_CACHE_SIZE = 16ULL * 1024 * 1024 * 1024;  // 16GB
constexpr int DEFAULT_WORKER_COUNT = 8;
constexpr int DEFAULT_LOOKAHEAD = 3;
constexpr size_t MAX_PREFETCH_QUEUE_SIZE = 100;

// S3 timeouts and retries
constexpr int URGENT_TIMEOUT_MS = 5000;
constexpr int PREFETCH_TIMEOUT_MS = 3000;
constexpr int URGENT_MAX_RETRIES = 3;
constexpr int PREFETCH_MAX_RETRIES = 0;  // Fail fast

// Helper to convert string to bytes (parses "16G", "512M", etc.)
inline size_t parse_size(const std::string& size_str) {
    if (size_str.empty()) return 0;

    size_t value = std::stoull(size_str);
    char suffix = size_str.back();

    switch (suffix) {
        case 'K': case 'k': return value * 1024;
        case 'M': case 'm': return value * 1024 * 1024;
        case 'G': case 'g': return value * 1024ULL * 1024 * 1024;
        default: return value;  // Assume bytes if no suffix
    }
}

// String conversion for enums (useful for logging)
inline const char* to_string(CacheZone zone) {
    switch (zone) {
        case CacheZone::HOT: return "HOT";
        case CacheZone::PREFETCH: return "PREFETCH";
        default: return "UNKNOWN";
    }
}

inline const char* to_string(Priority priority) {
    switch (priority) {
        case Priority::URGENT: return "URGENT";
        case Priority::NORMAL: return "NORMAL";
        case Priority::BACKGROUND: return "BACKGROUND";
        default: return "UNKNOWN";
    }
}

}  // namespace valkyrie
