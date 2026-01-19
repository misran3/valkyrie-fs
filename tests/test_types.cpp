#include "../src/types.hpp"
#include <cassert>
#include <iostream>

using namespace valkyrie;

void test_parse_size() {
    assert(parse_size("1024") == 1024);
    assert(parse_size("1K") == 1024);
    assert(parse_size("1M") == 1024 * 1024);
    assert(parse_size("1G") == 1024ULL * 1024 * 1024);
    assert(parse_size("16G") == 16ULL * 1024 * 1024 * 1024);
    std::cout << "test_parse_size: PASS\n";
}

void test_enum_to_string() {
    assert(std::string(to_string(CacheZone::HOT)) == "HOT");
    assert(std::string(to_string(Priority::URGENT)) == "URGENT");
    std::cout << "test_enum_to_string: PASS\n";
}

int main() {
    test_parse_size();
    test_enum_to_string();
    std::cout << "All types tests passed!\n";
    return 0;
}
