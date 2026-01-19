#include "../src/cache_manager.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace valkyrie;

void test_insert_and_get() {
    CacheManager cache(8 * 1024 * 1024);  // 8MB cache

    std::vector<char> data(1024, 'A');  // 1KB of 'A'
    cache.insert_chunk("file1.bin", 0, data, CacheZone::HOT);

    auto chunk = cache.get_chunk("file1.bin", 0);
    assert(chunk.has_value());
    assert(chunk->data.size() == 1024);
    assert(chunk->data[0] == 'A');

    std::cout << "test_insert_and_get: PASS\n";
}

void test_zone_promotion() {
    CacheManager cache(8 * 1024 * 1024);

    std::vector<char> data(1024, 'B');
    cache.insert_chunk("file2.bin", 0, data, CacheZone::PREFETCH);

    // Verify it's in PREFETCH zone
    assert(cache.get_zone("file2.bin") == CacheZone::PREFETCH);

    // Access the chunk (should promote to HOT)
    cache.access("file2.bin", 0);

    // Verify promotion
    assert(cache.get_zone("file2.bin") == CacheZone::HOT);

    std::cout << "test_zone_promotion: PASS\n";
}

void test_lru_eviction() {
    CacheManager cache(3 * 1024);  // 3KB total

    std::vector<char> data1(1024, '1');
    std::vector<char> data2(1024, '2');
    std::vector<char> data3(1024, '3');
    std::vector<char> data4(1024, '4');

    cache.insert_chunk("file1", 0, data1, CacheZone::HOT);
    cache.insert_chunk("file2", 0, data2, CacheZone::HOT);
    cache.insert_chunk("file3", 0, data3, CacheZone::HOT);

    // All three should fit
    assert(cache.contains("file1"));
    assert(cache.contains("file2"));
    assert(cache.contains("file3"));

    // Insert 4th file - should evict oldest (file1)
    cache.insert_chunk("file4", 0, data4, CacheZone::HOT);

    assert(!cache.contains("file1"));  // Evicted
    assert(cache.contains("file4"));

    std::cout << "test_lru_eviction: PASS\n";
}

void test_chunked_file() {
    CacheManager cache(16 * 1024 * 1024);

    // Insert multiple chunks of same file
    std::vector<char> chunk0(4096, 'A');
    std::vector<char> chunk1(4096, 'B');
    std::vector<char> chunk2(4096, 'C');

    cache.insert_chunk("large_file.bin", 0, chunk0, CacheZone::HOT);
    cache.insert_chunk("large_file.bin", 4096, chunk1, CacheZone::HOT);
    cache.insert_chunk("large_file.bin", 8192, chunk2, CacheZone::HOT);

    // Retrieve chunks
    auto c0 = cache.get_chunk("large_file.bin", 0);
    auto c1 = cache.get_chunk("large_file.bin", 4096);
    auto c2 = cache.get_chunk("large_file.bin", 8192);

    assert(c0.has_value() && c0->data[0] == 'A');
    assert(c1.has_value() && c1->data[0] == 'B');
    assert(c2.has_value() && c2->data[0] == 'C');

    std::cout << "test_chunked_file: PASS\n";
}

int main() {
    test_insert_and_get();
    test_zone_promotion();
    test_lru_eviction();
    test_chunked_file();
    std::cout << "All CacheManager tests passed!\n";
    return 0;
}
