#pragma once

#include "types.hpp"
#include "cache_manager.hpp"
#include "s3_worker_pool.hpp"

#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace valkyrie {

class Predictor {
public:
    Predictor(CacheManager& cache,
              S3WorkerPool& worker_pool,
              int lookahead = DEFAULT_LOOKAHEAD);

    ~Predictor();

    // Start predictor thread
    void start();

    // Stop predictor thread
    void stop();

    // Notify predictor of file access
    void on_file_accessed(const std::string& s3_key);

    // Load manifest file
    bool load_manifest(const std::string& manifest_path);

    // Static pattern detection (for testing)
    static std::optional<std::string> predict_next_sequential(const std::string& filename);

    struct Stats {
        std::atomic<uint64_t> predictions_made{0};
        std::atomic<uint64_t> prefetches_issued{0};
        std::atomic<uint64_t> pattern_hits{0};
        std::atomic<uint64_t> manifest_hits{0};
    };

    const Stats& get_stats() const { return stats_; }

private:
    void predictor_loop();
    void predict_and_prefetch(const std::string& s3_key);
    std::optional<int> find_in_manifest(const std::string& s3_key);

    CacheManager& cache_;
    S3WorkerPool& worker_pool_;
    int lookahead_;

    // Manifest mode
    bool manifest_mode_;
    std::vector<std::string> manifest_;

    // Recent access tracking
    std::string last_accessed_;
    std::mutex access_mutex_;

    // In-flight tracking (prevent duplicate prefetches)
    std::unordered_set<std::string> in_flight_;
    std::mutex in_flight_mutex_;

    // Predictor thread
    std::thread predictor_thread_;
    std::atomic<bool> stop_flag_;

    Stats stats_;
};

}  // namespace valkyrie
