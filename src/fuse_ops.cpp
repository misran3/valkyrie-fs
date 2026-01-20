#include "fuse_ops.hpp"
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

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
    (void) conn;
    try {
        std::cout << "Initializing FUSE filesystem (macFUSE)\n";

        FuseContext* ctx = get_valkyrie_context();
        ctx->start();

        return ctx;
    } catch (const std::exception& e) {
        std::cerr << "init error: " << e.what() << "\n";
        return nullptr;  // Signal mount failure to FUSE
    }
}
#else
void* init(struct fuse_conn_info* conn, struct fuse_config* cfg) {
    (void) conn;

    try {
        cfg->kernel_cache = 1;
        cfg->auto_cache = 1;
        cfg->entry_timeout = 300.0;
        cfg->attr_timeout = 300.0;
        cfg->negative_timeout = 60.0;

        std::cout << "Initializing FUSE filesystem (libfuse3)\n";

        FuseContext* ctx = get_valkyrie_context();
        ctx->start();

        return ctx;
    } catch (const std::exception& e) {
        std::cerr << "init error: " << e.what() << "\n";
        return nullptr;  // Signal mount failure to FUSE
    }
}
#endif

void destroy(void* private_data) {
    try {
        FuseContext* ctx = static_cast<FuseContext*>(private_data);

        if (ctx) {
            // Print statistics
            const auto& cache_stats = ctx->cache->get_stats();
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
    } catch (const std::exception& e) {
        std::cerr << "destroy error: " << e.what() << "\n";
        // Continue with cleanup - this is shutdown path
    }
}

#ifdef __APPLE__
int getattr(const char* path, struct stat* stbuf) {
    try {
        FuseContext* ctx = get_valkyrie_context();
        std::memset(stbuf, 0, sizeof(struct stat));

        // Root directory
        if (std::strcmp(path, "/") == 0) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            stbuf->st_ino = 1;
            stbuf->st_uid = getuid();
            stbuf->st_gid = getgid();
            stbuf->st_size = 4096;
            stbuf->st_blocks = 8;
            stbuf->st_blksize = 4096;
            return 0;
        }

        // Regular file - assume all non-root paths are files in S3
        std::string s3_key = path_to_s3_key(path);

        // Check if we have size metadata
        {
            std::shared_lock<std::shared_mutex> lock(ctx->metadata_mutex);
            auto it = ctx->file_sizes.find(s3_key);
            if (it != ctx->file_sizes.end()) {
                stbuf->st_mode = S_IFREG | 0444;  // Read-only
                stbuf->st_nlink = 1;
                stbuf->st_size = it->second;
                return 0;
            }
        }

        // File not in metadata cache - doesn't exist yet
        // TODO Phase 6.6: open() callback will populate metadata cache on first access
        // TODO Future: Add HeadObject request to verify existence in S3
        return -ENOENT;

    } catch (const std::exception& e) {
        std::cerr << "getattr error: " << e.what() << "\n";
        return -EIO;
    }
}
#else
int getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void) fi;  // Unused

    try {
        FuseContext* ctx = get_valkyrie_context();
        std::memset(stbuf, 0, sizeof(struct stat));

        // Root directory
        if (std::strcmp(path, "/") == 0) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            return 0;
        }

        // Regular file - assume all non-root paths are files in S3
        std::string s3_key = path_to_s3_key(path);

        // Check if we have size metadata
        {
            std::shared_lock<std::shared_mutex> lock(ctx->metadata_mutex);
            auto it = ctx->file_sizes.find(s3_key);
            if (it != ctx->file_sizes.end()) {
                stbuf->st_mode = S_IFREG | 0444;  // Read-only
                stbuf->st_nlink = 1;
                stbuf->st_size = it->second;
                return 0;
            }
        }

        // File not in metadata cache - doesn't exist yet
        // TODO Phase 6.6: open() callback will populate metadata cache on first access
        // TODO Future: Add HeadObject request to verify existence in S3
        return -ENOENT;

    } catch (const std::exception& e) {
        std::cerr << "getattr error: " << e.what() << "\n";
        return -EIO;
    }
}
#endif

#ifdef __APPLE__
int readdir(const char* path, void* buf, fuse_fill_dir_t filler,
            off_t offset, struct fuse_file_info* fi) {
    (void) offset;
    (void) fi;

    try {
        // Only support root directory listing
        if (std::strcmp(path, "/") != 0) {
            return -ENOENT;
        }

        // Add standard entries
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);

        // List all files from metadata cache
        FuseContext* ctx = get_valkyrie_context();
        std::shared_lock<std::shared_mutex> lock(ctx->metadata_mutex);

        for (const auto& [s3_key, size] : ctx->file_sizes) {
            filler(buf, s3_key.c_str(), NULL, 0);
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "readdir error: " << e.what() << "\n";
        return -EIO;
    }
}
#else
int readdir(const char* path, void* buf, fuse_fill_dir_t filler,
            off_t offset, struct fuse_file_info* fi,
            enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    try {
        // Only support root directory listing
        if (std::strcmp(path, "/") != 0) {
            return -ENOENT;
        }

        // Add standard entries
        filler(buf, ".", NULL, 0, (fuse_fill_dir_flags)0);
        filler(buf, "..", NULL, 0, (fuse_fill_dir_flags)0);

        // List all files from metadata cache
        FuseContext* ctx = get_valkyrie_context();
        std::shared_lock<std::shared_mutex> lock(ctx->metadata_mutex);

        for (const auto& [s3_key, size] : ctx->file_sizes) {
            filler(buf, s3_key.c_str(), NULL, 0, (fuse_fill_dir_flags)0);
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "readdir error: " << e.what() << "\n";
        return -EIO;
    }
}
#endif

int open(const char* path, struct fuse_file_info* fi) {
    try {
        // Only allow read-only access
        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            return -EACCES;
        }

        FuseContext* ctx = get_valkyrie_context();
        std::string s3_key = path_to_s3_key(path);

        // Notify predictor of file access
        ctx->predictor->on_file_accessed(s3_key);

        // Update metadata cache
        {
            std::unique_lock<std::shared_mutex> lock(ctx->metadata_mutex);
            if (ctx->file_sizes.find(s3_key) == ctx->file_sizes.end()) {
                // Add with default size (will be updated on first read)
                ctx->file_sizes[s3_key] = 1024 * 1024 * 1024;  // 1GB default
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "open error: " << e.what() << "\n";
        return -EIO;
    }
}

int release(const char* path, struct fuse_file_info* fi) {
    try {
        (void) path;
        (void) fi;
        // No cleanup needed for read-only files
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "release error: " << e.what() << "\n";
        return -EIO;
    }
}

int read(const char* path, char* buf, size_t size, off_t offset,
         struct fuse_file_info* fi) {
    try {
        (void) fi;

        FuseContext* ctx = get_valkyrie_context();
        std::string s3_key = path_to_s3_key(path);

        // Determine chunk offset
        size_t chunk_offset = (offset / DEFAULT_CHUNK_SIZE) * DEFAULT_CHUNK_SIZE;
        size_t offset_in_chunk = offset % DEFAULT_CHUNK_SIZE;

        // Try to get chunk from cache
        auto chunk_opt = ctx->cache->get_chunk(s3_key, chunk_offset);

        if (!chunk_opt.has_value()) {
            // CACHE MISS - Block and download with URGENT priority
            std::cout << "Cache miss: " << s3_key << " at offset " << offset << "\n";

            // Submit URGENT download request
            auto future = ctx->worker_pool->submit(
                s3_key, chunk_offset, DEFAULT_CHUNK_SIZE, Priority::URGENT
            );

            // Wait for download (blocks FUSE thread)
            bool success = future.get();

            if (!success) {
                std::cerr << "Failed to download chunk: " << s3_key << " offset " << chunk_offset << "\n";
                return -EIO;  // I/O error
            }

            // Retrieve from cache (should be present now)
            chunk_opt = ctx->cache->get_chunk(s3_key, chunk_offset);
            if (!chunk_opt.has_value()) {
                std::cerr << "Chunk missing after download: " << s3_key << "\n";
                return -EIO;
            }
        }

        // Mark as accessed BEFORE dereferencing to minimize race window
        // While the chunk data is copied (safe even if evicted), we must ensure
        // access() is called as close to get_chunk() as possible to maintain
        // accurate LRU statistics and prevent accessing stale cache entries.
        ctx->cache->access(s3_key, chunk_offset);

        const auto& chunk = *chunk_opt;

        // Calculate how much to copy from this chunk
        size_t available = chunk.data.size() - offset_in_chunk;
        size_t to_copy = std::min(size, available);

        // Copy data to FUSE buffer
        std::memcpy(buf, chunk.data.data() + offset_in_chunk, to_copy);

        // If we need more data (crossing chunk boundary), recursively read next chunk
        if (to_copy < size) {
            // Read from next chunk
            int bytes_read = read(path, buf + to_copy, size - to_copy,
                                 offset + to_copy, fi);
            if (bytes_read < 0) {
                return bytes_read;  // Propagate error
            }
            to_copy += bytes_read;
        }

        return to_copy;
    } catch (const std::exception& e) {
        std::cerr << "read error: " << e.what() << "\n";
        return -EIO;
    }
}

}  // namespace fuse_ops

}  // namespace valkyrie
