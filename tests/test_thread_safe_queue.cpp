#include "../src/thread_safe_queue.hpp"
#include <thread>
#include <vector>
#include <cassert>
#include <iostream>

using namespace valkyrie;

void test_basic_push_pop() {
    ThreadSafeQueue<int> queue;

    queue.push(42, Priority::NORMAL);

    auto item = queue.pop();
    assert(item.has_value());
    assert(item->data == 42);
    assert(item->priority == Priority::NORMAL);

    std::cout << "test_basic_push_pop: PASS\n";
}

void test_priority_ordering() {
    ThreadSafeQueue<int> queue;

    queue.push(1, Priority::BACKGROUND);
    queue.push(2, Priority::URGENT);
    queue.push(3, Priority::NORMAL);

    // Should pop in priority order: URGENT > NORMAL > BACKGROUND
    auto item1 = queue.pop();
    assert(item1->data == 2);
    assert(item1->priority == Priority::URGENT);

    auto item2 = queue.pop();
    assert(item2->data == 3);

    auto item3 = queue.pop();
    assert(item3->data == 1);

    std::cout << "test_priority_ordering: PASS\n";
}

void test_blocking_pop() {
    ThreadSafeQueue<int> queue;
    bool popped = false;

    std::thread consumer([&]() {
        auto item = queue.pop();  // Blocks until item available
        assert(item->data == 99);
        popped = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    queue.push(99, Priority::NORMAL);

    consumer.join();
    assert(popped);

    std::cout << "test_blocking_pop: PASS\n";
}

void test_shutdown() {
    ThreadSafeQueue<int> queue;

    std::thread consumer([&]() {
        auto item = queue.pop();
        assert(!item.has_value());  // Returns empty on shutdown
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    queue.shutdown();

    consumer.join();

    std::cout << "test_shutdown: PASS\n";
}

int main() {
    test_basic_push_pop();
    test_priority_ordering();
    test_blocking_pop();
    test_shutdown();
    std::cout << "All ThreadSafeQueue tests passed!\n";
    return 0;
}
