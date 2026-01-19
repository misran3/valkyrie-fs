#pragma once

#include "types.hpp"
#include "cache_manager.hpp"
#include "thread_safe_queue.hpp"

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <future>

namespace valkyrie {

// S3 download task
struct PrefetchTask {
    std::string s3_key;           // Object key in S3
    size_t offset;                // Chunk offset
    size_t size;                  // Chunk size
    Priority priority;            // URGENT, NORMAL, BACKGROUND
    std::shared_ptr<std::promise<bool>> completion;  // Fulfill when done

    PrefetchTask(const std::string& key, size_t off, size_t sz, Priority prio)
        : s3_key(key)
        , offset(off)
        , size(sz)
        , priority(prio)
        , completion(std::make_shared<std::promise<bool>>()) {}
};

struct S3Config {
    std::string bucket;
    std::string region;
    std::string prefix;  // Optional key prefix

    std::string get_full_key(const std::string& file_key) const {
        return prefix.empty() ? file_key : prefix + "/" + file_key;
    }
};

class S3WorkerPool {
public:
    S3WorkerPool(const S3Config& config,
                 CacheManager& cache,
                 int num_workers = DEFAULT_WORKER_COUNT);

    ~S3WorkerPool();

    // Start worker threads
    void start();

    // Submit task and get future
    std::shared_future<bool> submit(const std::string& s3_key,
                                    size_t offset,
                                    size_t size,
                                    Priority priority);

    // Shutdown workers
    void shutdown();

    // Statistics
    struct Stats {
        std::atomic<uint64_t> total_downloads{0};
        std::atomic<uint64_t> successful_downloads{0};
        std::atomic<uint64_t> failed_downloads{0};
        std::atomic<uint64_t> bytes_downloaded{0};
    };

    const Stats& get_stats() const { return stats_; }

private:
    void worker_loop(int worker_id);
    bool download_chunk(const PrefetchTask& task);

    S3Config config_;
    CacheManager& cache_;
    int num_workers_;

    ThreadSafeQueue<PrefetchTask> task_queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_flag_;

    // AWS SDK components
    Aws::SDKOptions sdk_options_;
    std::unique_ptr<Aws::S3::S3Client> s3_client_;

    Stats stats_;
};

}  // namespace valkyrie
