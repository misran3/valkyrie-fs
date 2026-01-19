#include "metrics_server.hpp"
#include <iostream>
#include <sstream>

namespace valkyrie {

MetricsServer::MetricsServer(int port,
                             CacheManager& cache,
                             S3WorkerPool& worker_pool,
                             Predictor& predictor)
    : port_(port)
    , cache_(cache)
    , worker_pool_(worker_pool)
    , predictor_(predictor)
    , stop_flag_(false) {
}

MetricsServer::~MetricsServer() {
    stop();
}

void MetricsServer::start() {
    std::cout << "Metrics server: Disabled (not implemented in MVP)\n";
    std::cout << "To view metrics, check statistics at shutdown or use Logger\n";
    // server_thread_ = std::thread(&MetricsServer::server_loop, this);
}

void MetricsServer::stop() {
    stop_flag_ = true;
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void MetricsServer::server_loop() {
    // TODO: Implement HTTP server using cpp-httplib
    // For now, this is a no-op stub
}

std::string MetricsServer::generate_prometheus_metrics() {
    const auto& cache_stats = cache_.get_stats();
    const auto& worker_stats = worker_pool_.get_stats();
    // predictor_stats not needed for MVP metrics

    std::ostringstream oss;

    // Prometheus format
    oss << "# HELP valkyrie_cache_size_bytes Current cache size in bytes\n";
    oss << "# TYPE valkyrie_cache_size_bytes gauge\n";
    oss << "valkyrie_cache_size_bytes " << cache_stats.current_size << "\n\n";

    oss << "# HELP valkyrie_downloads_total Total S3 downloads\n";
    oss << "# TYPE valkyrie_downloads_total counter\n";
    oss << "valkyrie_downloads_total " << worker_stats.total_downloads << "\n\n";

    return oss.str();
}

}  // namespace valkyrie
