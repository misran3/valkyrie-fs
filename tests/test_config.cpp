#include "../src/config.hpp"
#include <cassert>
#include <iostream>
#include <cstring>

using namespace valkyrie;

void test_minimal_config() {
    const char* argv[] = {
        "valkyrie",
        "--mount", "/tmp/valkyrie",
        "--bucket", "my-bucket",
        "--region", "us-west-2"
    };
    int argc = 7;

    Config config;
    bool success = config.parse(argc, const_cast<char**>(argv));

    assert(success);
    assert(config.mount_point == "/tmp/valkyrie");
    assert(config.s3_config.bucket == "my-bucket");
    assert(config.s3_config.region == "us-west-2");

    // Defaults
    assert(config.cache_size == DEFAULT_CACHE_SIZE);
    assert(config.num_workers == DEFAULT_WORKER_COUNT);

    std::cout << "test_minimal_config: PASS\n";
}

void test_full_config() {
    const char* argv[] = {
        "valkyrie",
        "--mount", "/mnt/data",
        "--bucket", "training-data",
        "--region", "eu-west-1",
        "--s3-prefix", "shards",
        "--cache-size", "8G",
        "--workers", "16",
        "--lookahead", "5",
        "--manifest", "files.txt"
    };
    int argc = 17;

    Config config;
    bool success = config.parse(argc, const_cast<char**>(argv));

    assert(success);
    assert(config.mount_point == "/mnt/data");
    assert(config.s3_config.bucket == "training-data");
    assert(config.s3_config.region == "eu-west-1");
    assert(config.s3_config.prefix == "shards");
    assert(config.cache_size == 8ULL * 1024 * 1024 * 1024);
    assert(config.num_workers == 16);
    assert(config.lookahead == 5);
    assert(config.manifest_path == "files.txt");

    std::cout << "test_full_config: PASS\n";
}

void test_missing_required() {
    const char* argv[] = {
        "valkyrie",
        "--mount", "/tmp/test"
        // Missing bucket and region
    };
    int argc = 3;

    Config config;
    bool success = config.parse(argc, const_cast<char**>(argv));

    assert(!success);  // Should fail

    std::cout << "test_missing_required: PASS\n";
}

void test_invalid_cache_size() {
    const char* argv[] = {
        "valkyrie",
        "--mount", "/tmp/test",
        "--bucket", "test",
        "--region", "us-west-2",
        "--cache-size", "invalid"
    };
    int argc = 9;

    Config config;
    bool success = config.parse(argc, const_cast<char**>(argv));

    assert(!success);  // Should fail validation

    std::cout << "test_invalid_cache_size: PASS\n";
}

int main() {
    test_minimal_config();
    test_full_config();
    test_missing_required();
    test_invalid_cache_size();
    std::cout << "All Config tests passed!\n";
    return 0;
}
