#pragma once

#include "types.hpp"
#include "s3_worker_pool.hpp"
#include <string>
#include <optional>

namespace valkyrie {

struct Config {
    // Required
    std::string mount_point;
    S3Config s3_config;  // bucket, region, prefix

    // Optional with defaults
    size_t cache_size = DEFAULT_CACHE_SIZE;
    int num_workers = DEFAULT_WORKER_COUNT;
    int lookahead = DEFAULT_LOOKAHEAD;
    std::string manifest_path;
    int metrics_port = 9090;
    bool enable_tracing = false;
    std::string trace_output = "trace.json";

    // Parse from command line
    bool parse(int argc, char* argv[]);

    // Validate configuration
    bool validate() const;

    // Print usage
    static void print_usage(const char* program_name);

private:
    bool parse_cache_size(const std::string& size_str);
};

}  // namespace valkyrie
