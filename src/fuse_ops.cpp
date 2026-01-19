#include "fuse_ops.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

namespace valkyrie {

FuseContext::FuseContext(const Config& cfg)
    : config(cfg) {

    std::cout << "Initializing Valkyrie-FS...\n";

    try {
        // Create cache
        cache = std::make_unique<CacheManager>(config.cache_size);
        std::cout << "Cache initialized: " << (config.cache_size / (1024*1024)) << "MB\n";

        // Create S3 worker pool
        worker_pool = std::make_unique<S3WorkerPool>(
            config.s3_config, *cache, config.num_workers
        );
        std::cout << "S3 worker pool created: " << config.num_workers << " workers\n";

        // Create predictor
        predictor = std::make_unique<Predictor>(
            *cache, *worker_pool, config.lookahead
        );
        std::cout << "Predictor created: lookahead=" << config.lookahead << "\n";

        // Load manifest if specified
        if (!config.manifest_path.empty()) {
            if (predictor->load_manifest(config.manifest_path)) {
                std::cout << "Manifest loaded: " << config.manifest_path << "\n";
            } else {
                std::cerr << "WARNING: Failed to load manifest\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Failed to initialize Valkyrie-FS: " << e.what() << "\n";
        throw;  // Re-throw to allow caller to handle
    }
}

FuseContext::~FuseContext() {
    stop();
}

void FuseContext::start() {
    bool expected = false;
    if (!is_started.compare_exchange_strong(expected, true)) {
        std::cerr << "WARNING: Valkyrie-FS already started\n";
        return;
    }

    worker_pool->start();
    predictor->start();
    std::cout << "Valkyrie-FS started successfully\n";
}

void FuseContext::stop() {
    bool expected = true;
    if (!is_started.compare_exchange_strong(expected, false)) {
        return;  // Already stopped
    }

    std::cout << "Shutting down Valkyrie-FS...\n";

    if (predictor) {
        predictor->stop();
    }

    if (worker_pool) {
        worker_pool->shutdown();
    }

    std::cout << "Valkyrie-FS stopped\n";
}

FuseContext* get_valkyrie_context() {
    auto* fuse_ctx = fuse_get_context();
    if (!fuse_ctx || !fuse_ctx->private_data) {
        throw std::runtime_error("FUSE context not initialized");
    }
    return static_cast<FuseContext*>(fuse_ctx->private_data);
}

std::string path_to_s3_key(const char* path) {
    // Strip leading slash
    std::string key = path;
    if (!key.empty() && key[0] == '/') {
        key = key.substr(1);
    }
    return key;
}

namespace fuse_ops {

#ifdef __APPLE__
void* init(struct fuse_conn_info* conn) {
    (void) conn;  // Unused

    // macFUSE doesn't have fuse_config - configuration is done via mount options
    std::cout << "Initializing FUSE filesystem (macFUSE)\n";

    FuseContext* ctx = get_valkyrie_context();
    ctx->start();

    return ctx;
}
#else
void* init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
    (void) conn;  // Unused

    // Configure FUSE
    cfg->kernel_cache = 1;  // Enable kernel caching for performance
    cfg->auto_cache = 1;
    cfg->entry_timeout = 300.0;  // 5 minutes
    cfg->attr_timeout = 300.0;
    cfg->negative_timeout = 60.0;  // 1 minute for negative entries

    std::cout << "Initializing FUSE filesystem (libfuse3)\n";

    FuseContext* ctx = get_valkyrie_context();
    ctx->start();

    return ctx;
}
#endif

void destroy(void* private_data) {
    FuseContext* ctx = static_cast<FuseContext*>(private_data);

    if (ctx) {
        // Print statistics
        auto cache_stats = ctx->cache->get_stats();
        const auto& worker_stats = ctx->worker_pool->get_stats();
        const auto& predictor_stats = ctx->predictor->get_stats();

        std::cout << "\n=== Valkyrie-FS Statistics ===\n";
        std::cout << "Cache:\n";
        std::cout << "  Current size: " << (cache_stats.current_size / (1024*1024)) << "MB\n";
        std::cout << "  HOT zone: " << (cache_stats.hot_zone_size / (1024*1024)) << "MB\n";
        std::cout << "  PREFETCH zone: " << (cache_stats.prefetch_zone_size / (1024*1024)) << "MB\n";
        std::cout << "  Files cached: " << cache_stats.num_files << "\n";
        std::cout << "  Chunks cached: " << cache_stats.num_chunks << "\n";

        std::cout << "S3 Downloads:\n";
        std::cout << "  Total: " << worker_stats.total_downloads.load() << "\n";
        std::cout << "  Successful: " << worker_stats.successful_downloads.load() << "\n";
        std::cout << "  Failed: " << worker_stats.failed_downloads.load() << "\n";
        std::cout << "  Bytes downloaded: " << (worker_stats.bytes_downloaded.load() / (1024*1024)) << "MB\n";

        std::cout << "Predictor:\n";
        std::cout << "  Predictions made: " << predictor_stats.predictions_made.load() << "\n";
        std::cout << "  Prefetches issued: " << predictor_stats.prefetches_issued.load() << "\n";
        std::cout << "  Pattern hits: " << predictor_stats.pattern_hits.load() << "\n";
        std::cout << "  Manifest hits: " << predictor_stats.manifest_hits.load() << "\n";

        ctx->stop();
    }
}

}  // namespace fuse_ops

}  // namespace valkyrie
