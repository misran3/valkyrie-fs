#include "fuse_ops.hpp"
#include <iostream>
#include <cstring>
#include <errno.h>

namespace valkyrie {

FuseContext::FuseContext(const Config& cfg)
    : config(cfg) {

    std::cout << "Initializing Valkyrie-FS...\n";

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
}

FuseContext::~FuseContext() {
    stop();
}

void FuseContext::start() {
    worker_pool->start();
    predictor->start();
    std::cout << "Valkyrie-FS started successfully\n";
}

void FuseContext::stop() {
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
    return static_cast<FuseContext*>(fuse_get_context()->private_data);
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
