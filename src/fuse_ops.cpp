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

}  // namespace valkyrie
