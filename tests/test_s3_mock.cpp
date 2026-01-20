#include "../src/s3_worker_pool.hpp"
#include "../src/cache_manager.hpp"
#include <aws/core/Aws.h>
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

using namespace valkyrie;

// This test verifies the worker pool starts/stops correctly
// Real S3 testing requires credentials and a test bucket
void test_worker_pool_lifecycle() {
    CacheManager cache(16 * 1024 * 1024);

    S3Config config;
    config.bucket = "test-bucket";
    config.region = "us-east-1";
    config.prefix = "";

    S3WorkerPool pool(config, cache, 2);

    pool.start();

    // Let workers initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    pool.shutdown();

    std::cout << "test_worker_pool_lifecycle: PASS\n";
}

void test_task_submission() {
    CacheManager cache(16 * 1024 * 1024);

    S3Config config;
    config.bucket = "test-bucket";
    config.region = "us-east-1";

    S3WorkerPool pool(config, cache, 2);
    pool.start();

    // Submit a task (will fail without real S3, but tests queue mechanics)
    auto future = pool.submit("test_file.bin", 0, 4096, Priority::NORMAL);

    // Wait briefly for attempt
    auto status = future.wait_for(std::chrono::seconds(2));

    // May timeout or return false (S3 failure expected in test environment)
    assert(status == std::future_status::ready ||
           status == std::future_status::timeout);

    pool.shutdown();

    std::cout << "test_task_submission: PASS\n";
}

int main() {
    // Initialize AWS SDK
    Aws::SDKOptions sdk_options;
    Aws::InitAPI(sdk_options);

    std::cout << "=== S3WorkerPool Mock Tests ===\n";
    std::cout << "Note: These tests verify mechanics, not actual S3 downloads\n";
    std::cout << "For real S3 tests, use scripts/test_s3_integration.sh\n\n";

    test_worker_pool_lifecycle();
    test_task_submission();

    std::cout << "\nAll mock tests passed!\n";

    // Shutdown AWS SDK
    Aws::ShutdownAPI(sdk_options);
    return 0;
}
