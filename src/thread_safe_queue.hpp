#pragma once

#include "types.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>

namespace valkyrie {

template <typename T>
struct QueueItem {
    T data;
    Priority priority;

    // For priority queue ordering (higher priority = lower value for max-heap)
    bool operator<(const QueueItem& other) const {
        return priority > other.priority;  // Reversed for max-heap behavior
    }
};

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() : shutdown_flag_(false) {}

    // Push item with priority
    void push(T data, Priority priority) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_flag_) return;  // Don't accept new items after shutdown

            queue_.push({std::move(data), priority});
        }
        cv_.notify_one();
    }

    // Pop item (blocks if queue empty)
    // Returns std::nullopt if shutdown
    std::optional<QueueItem<T>> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait until queue has items or shutdown
        cv_.wait(lock, [this]() {
            return !queue_.empty() || shutdown_flag_;
        });

        if (shutdown_flag_ && queue_.empty()) {
            return std::nullopt;
        }

        QueueItem<T> item = queue_.top();
        queue_.pop();
        return item;
    }

    // Try pop (non-blocking)
    std::optional<QueueItem<T>> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        QueueItem<T> item = queue_.top();
        queue_.pop();
        return item;
    }

    // Shutdown queue (wakes all waiting threads)
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_flag_ = true;
        }
        cv_.notify_all();
    }

    // Check if empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Get size
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<QueueItem<T>> queue_;
    std::atomic<bool> shutdown_flag_;
};

}  // namespace valkyrie
