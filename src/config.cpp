#include "config.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

namespace valkyrie {

bool Config::parse(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    // Simple argument parser (no external dependencies)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--mount") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --mount requires an argument\n";
                return false;
            }
            mount_point = argv[++i];
        }
        else if (arg == "--bucket") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --bucket requires an argument\n";
                return false;
            }
            s3_config.bucket = argv[++i];
        }
        else if (arg == "--region") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --region requires an argument\n";
                return false;
            }
            s3_config.region = argv[++i];
        }
        else if (arg == "--s3-prefix") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --s3-prefix requires an argument\n";
                return false;
            }
            s3_config.prefix = argv[++i];
        }
        else if (arg == "--cache-size") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --cache-size requires an argument\n";
                return false;
            }
            if (!parse_cache_size(argv[++i])) {
                return false;
            }
        }
        else if (arg == "--workers") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --workers requires an argument\n";
                return false;
            }
            try {
                num_workers = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid value for --workers\n";
                return false;
            }
        }
        else if (arg == "--lookahead") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --lookahead requires an argument\n";
                return false;
            }
            try {
                lookahead = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid value for --lookahead\n";
                return false;
            }
        }
        else if (arg == "--manifest") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --manifest requires an argument\n";
                return false;
            }
            manifest_path = argv[++i];
        }
        else if (arg == "--metrics-port") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --metrics-port requires an argument\n";
                return false;
            }
            try {
                metrics_port = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid value for --metrics-port\n";
                return false;
            }
        }
        else if (arg == "--enable-tracing") {
            enable_tracing = true;
        }
        else if (arg == "--trace-output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --trace-output requires an argument\n";
                return false;
            }
            trace_output = argv[++i];
        }
        else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        }
        else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }
    }

    // Validate after parsing
    return validate();
}

bool Config::validate() const {
    // Check required parameters
    if (mount_point.empty()) {
        std::cerr << "Error: --mount is required\n";
        return false;
    }

    if (s3_config.bucket.empty()) {
        std::cerr << "Error: --bucket is required\n";
        return false;
    }

    if (s3_config.region.empty()) {
        std::cerr << "Error: --region is required\n";
        return false;
    }

    // Validate numeric ranges
    if (cache_size < 1024 * 1024) {  // Minimum 1MB
        std::cerr << "Error: cache size must be at least 1MB\n";
        return false;
    }

    if (num_workers < 1 || num_workers > 128) {
        std::cerr << "Error: workers must be between 1 and 128\n";
        return false;
    }

    if (lookahead < 1 || lookahead > 256) {
        std::cerr << "Error: lookahead must be between 1 and 256\n";
        return false;
    }

    if (metrics_port < 1024 || metrics_port > 65535) {
        std::cerr << "Error: metrics port must be between 1024 and 65535\n";
        return false;
    }

    return true;
}

void Config::print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Required options:\n"
              << "  --mount PATH            Mount point for the filesystem\n"
              << "  --bucket NAME           S3 bucket name\n"
              << "  --region REGION         AWS region (e.g., us-west-2)\n\n"
              << "Optional options:\n"
              << "  --s3-prefix PREFIX      S3 key prefix (default: empty)\n"
              << "  --cache-size SIZE       Cache size (e.g., 16G, 512M) (default: 16GB)\n"
              << "  --workers N             Number of S3 worker threads (1-128) (default: 8)\n"
              << "  --lookahead N           Prefetch lookahead count (1-256) (default: 3)\n"
              << "  --manifest PATH         File containing list of S3 keys to prefetch\n"
              << "  --metrics-port PORT     Prometheus metrics port (default: 9090)\n"
              << "  --enable-tracing        Enable performance tracing\n"
              << "  --trace-output PATH     Trace output file (default: trace.json)\n"
              << "  --help, -h              Show this help message\n\n"
              << "Examples:\n"
              << "  " << program_name << " --mount /tmp/data --bucket my-bucket --region us-west-2\n"
              << "  " << program_name << " --mount /mnt/ml --bucket training-data --region eu-west-1 \\\n"
              << "                    --s3-prefix shards --cache-size 32G --workers 16\n";
}

bool Config::parse_cache_size(const std::string& size_str) {
    try {
        cache_size = parse_size(size_str);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid cache size format: " << size_str << "\n";
        std::cerr << "Expected format: number followed by K/M/G (e.g., 16G, 512M)\n";
        return false;
    }
}

}  // namespace valkyrie
