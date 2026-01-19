#pragma once

#include "cache_manager.hpp"
#include "s3_worker_pool.hpp"
#include "predictor.hpp"
#include <memory>
#include <atomic>
#include <thread>

namespace valkyrie {

class MetricsServer {
public:
    MetricsServer(int port,
                  CacheManager& cache,
                  S3WorkerPool& worker_pool,
                  Predictor& predictor);

    ~MetricsServer();

    void start();
    void stop();

private:
    void server_loop();
    std::string generate_prometheus_metrics();

    int port_;
    CacheManager& cache_;
    S3WorkerPool& worker_pool_;
    Predictor& predictor_;

    std::thread server_thread_;
    std::atomic<bool> stop_flag_;
};

}  // namespace valkyrie
