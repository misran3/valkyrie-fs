#pragma once

// Platform-specific FUSE headers
#ifdef __APPLE__
    #define FUSE_USE_VERSION 29  // macFUSE uses older API
    #include <fuse/fuse.h>
#else
    #define FUSE_USE_VERSION 31  // libfuse3
    #include <fuse3/fuse.h>
#endif

#include "config.hpp"
#include "cache_manager.hpp"
#include "s3_worker_pool.hpp"
#include "predictor.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace valkyrie {

// Global context passed to FUSE callbacks
struct FuseContext {
    std::unique_ptr<CacheManager> cache;
    std::unique_ptr<S3WorkerPool> worker_pool;
    std::unique_ptr<Predictor> predictor;

    Config config;

    // File metadata cache (s3_key -> size)
    std::unordered_map<std::string, size_t> file_sizes;
    mutable std::shared_mutex metadata_mutex;

    FuseContext(const Config& cfg);
    ~FuseContext();

    void start();
    void stop();

private:
    std::atomic<bool> is_started{false};
};

// FUSE operation callbacks
namespace fuse_ops {

#ifdef __APPLE__
void* init(struct fuse_conn_info* conn);  // macFUSE doesn't have fuse_config
#else
void* init(struct fuse_conn_info* conn, struct fuse_config* cfg);
#endif
void destroy(void* private_data);

int getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
#ifdef __APPLE__
int readdir(const char* path, void* buf, fuse_fill_dir_t filler,
            off_t offset, struct fuse_file_info* fi);
#else
int readdir(const char* path, void* buf, fuse_fill_dir_t filler,
            off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);
#endif

int open(const char* path, struct fuse_file_info* fi);
int read(const char* path, char* buf, size_t size, off_t offset,
         struct fuse_file_info* fi);

int release(const char* path, struct fuse_file_info* fi);

}  // namespace fuse_ops

// Helper to get FuseContext from fuse_get_context()
FuseContext* get_valkyrie_context();

// Helper to strip leading slash from path
std::string path_to_s3_key(const char* path);

}  // namespace valkyrie
