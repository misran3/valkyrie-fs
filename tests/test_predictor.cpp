#include "../src/predictor.hpp"
#include <cassert>
#include <iostream>

using namespace valkyrie;

void test_simple_sequential() {
    auto next = Predictor::predict_next_sequential("shard_042.bin");
    assert(next.has_value());
    assert(*next == "shard_043.bin");

    std::cout << "test_simple_sequential: PASS\n";
}

void test_zero_padded() {
    auto next1 = Predictor::predict_next_sequential("data_0001.tar");
    assert(next1.has_value());
    assert(*next1 == "data_0002.tar");

    auto next2 = Predictor::predict_next_sequential("file_00099.bin");
    assert(next2.has_value());
    assert(*next2 == "file_00100.bin");

    std::cout << "test_zero_padded: PASS\n";
}

void test_no_padding() {
    auto next = Predictor::predict_next_sequential("chunk9.bin");
    assert(next.has_value());
    assert(*next == "chunk10.bin");

    std::cout << "test_no_padding: PASS\n";
}

void test_no_pattern() {
    auto next = Predictor::predict_next_sequential("random_file.bin");
    assert(!next.has_value());

    std::cout << "test_no_pattern: PASS\n";
}

void test_rollover() {
    // 999 -> 1000 should work
    auto next = Predictor::predict_next_sequential("shard_999.bin");
    assert(next.has_value());
    assert(*next == "shard_1000.bin");

    std::cout << "test_rollover: PASS\n";
}

void test_manifest_loading() {
    CacheManager cache(16 * 1024 * 1024);

    S3Config config;
    config.bucket = "test";
    config.region = "us-east-1";

    S3WorkerPool pool(config, cache, 2);
    Predictor predictor(cache, pool, 3);

    bool loaded = predictor.load_manifest("tests/test_manifest.txt");
    assert(loaded);
    assert(predictor.get_manifest_size() == 5);  // Verify correct count

    std::cout << "test_manifest_loading: PASS\n";
}

int main() {
    test_simple_sequential();
    test_zero_padded();
    test_no_padding();
    test_no_pattern();
    test_rollover();
    test_manifest_loading();
    std::cout << "All Predictor tests passed!\n";
    return 0;
}
