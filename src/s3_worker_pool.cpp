#include "s3_worker_pool.hpp"
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <iostream>

namespace valkyrie {

S3WorkerPool::S3WorkerPool(const S3Config& config,
                           CacheManager& cache,
                           int num_workers)
    : config_(config)
    , cache_(cache)
    , num_workers_(num_workers)
    , shutdown_flag_(false) {

    // Initialize AWS SDK
    Aws::InitAPI(sdk_options_);

    // Create S3 client
    Aws::Client::ClientConfiguration client_config;
    client_config.region = config_.region;
    client_config.maxConnections = num_workers * 2;  // Allow parallel requests

    s3_client_ = std::make_unique<Aws::S3::S3Client>(client_config);

    std::cout << "S3WorkerPool initialized: bucket=" << config_.bucket
              << ", region=" << config_.region
              << ", workers=" << num_workers_ << "\n";
}

S3WorkerPool::~S3WorkerPool() {
    shutdown();

    // Cleanup AWS SDK
    Aws::ShutdownAPI(sdk_options_);
}

void S3WorkerPool::start() {
    for (int i = 0; i < num_workers_; ++i) {
        workers_.emplace_back(&S3WorkerPool::worker_loop, this, i);
    }
    std::cout << "S3WorkerPool: Started " << num_workers_ << " workers\n";
}

void S3WorkerPool::shutdown() {
    if (shutdown_flag_.exchange(true)) {
        return;  // Already shutdown
    }

    std::cout << "S3WorkerPool: Shutting down...\n";

    task_queue_.shutdown();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    std::cout << "S3WorkerPool: All workers stopped\n";
}

std::shared_future<bool> S3WorkerPool::submit(const std::string& s3_key,
                                              size_t offset,
                                              size_t size,
                                              Priority priority) {
    PrefetchTask task(s3_key, offset, size, priority);
    auto future = task.completion->get_future().share();

    task_queue_.push(std::move(task), priority);

    return future;
}

void S3WorkerPool::worker_loop(int worker_id) {
    while (!shutdown_flag_) {
        auto task_opt = task_queue_.pop();

        if (!task_opt.has_value()) {
            break;  // Shutdown signal
        }

        auto& task = task_opt->data;

        // Attempt download
        bool success = download_chunk(task);

        // Fulfill promise
        try {
            task.completion->set_value(success);
        } catch (const std::future_error& e) {
            // Promise already set (shouldn't happen, but handle gracefully)
            std::cerr << "Worker " << worker_id << ": Promise error: " << e.what() << "\n";
        }
    }
}

bool S3WorkerPool::download_chunk(const PrefetchTask& task) {
    stats_.total_downloads++;

    // Build full S3 key
    std::string full_key = config_.get_full_key(task.s3_key);

    // Create GetObject request with byte range
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(config_.bucket);
    request.SetKey(full_key);

    // Set byte range for chunk
    std::string range = "bytes=" + std::to_string(task.offset) + "-" +
                        std::to_string(task.offset + task.size - 1);
    request.SetRange(range);

    // Configure timeout based on priority
    int timeout_ms = (task.priority == Priority::URGENT)
                     ? URGENT_TIMEOUT_MS
                     : PREFETCH_TIMEOUT_MS;

    // Execute request
    auto outcome = s3_client_->GetObject(request);

    if (!outcome.IsSuccess()) {
        const auto& error = outcome.GetError();

        if (task.priority == Priority::URGENT) {
            std::cerr << "S3 GetObject failed (URGENT): " << full_key
                      << " at offset " << task.offset
                      << " - " << error.GetMessage() << "\n";
        }

        stats_.failed_downloads++;
        return false;
    }

    // Read response body
    auto& stream = outcome.GetResult().GetBody();
    std::vector<char> data(task.size);
    stream.read(data.data(), task.size);
    size_t bytes_read = stream.gcount();

    if (bytes_read == 0) {
        std::cerr << "S3 GetObject returned 0 bytes: " << full_key << "\n";
        stats_.failed_downloads++;
        return false;
    }

    // Resize if we read less than expected (end of file)
    if (bytes_read < task.size) {
        data.resize(bytes_read);
    }

    // Store in cache (promote to HOT if URGENT, otherwise PREFETCH)
    CacheZone zone = (task.priority == Priority::URGENT)
                     ? CacheZone::HOT
                     : CacheZone::PREFETCH;

    cache_.insert_chunk(task.s3_key, task.offset, data, zone);

    stats_.successful_downloads++;
    stats_.bytes_downloaded += bytes_read;

    return true;
}

}  // namespace valkyrie
