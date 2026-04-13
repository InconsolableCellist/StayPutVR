#pragma once

#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "Logger.hpp"

namespace StayPutVR {

// A single-threaded bounded work queue that replaces unbounded
// std::thread(...).detach() patterns. Items beyond max_size are dropped
// (logged). Shutdown() stops accepting new work and drains in-flight items.
class AsyncWorkQueue {
public:
    explicit AsyncWorkQueue(size_t max_size = 32)
        : max_size_(max_size), running_(false) {}

    ~AsyncWorkQueue() { Shutdown(); }

    void Start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) return;
        worker_ = std::thread(&AsyncWorkQueue::WorkerLoop, this);
    }

    void Shutdown() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) return;
        cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    bool Enqueue(std::function<void()> work) {
        if (!running_) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= max_size_) {
                Logger::Warning("AsyncWorkQueue: queue full (" +
                                std::to_string(max_size_) +
                                "), dropping work item");
                return false;
            }
            queue_.push(std::move(work));
        }
        cv_.notify_one();
        return true;
    }

private:
    void WorkerLoop() {
        while (true) {
            std::function<void()> work;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
                if (!running_ && queue_.empty()) return;
                if (queue_.empty()) continue;
                work = std::move(queue_.front());
                queue_.pop();
            }
            try {
                work();
            } catch (const std::exception& e) {
                Logger::Error("AsyncWorkQueue: work item threw: " +
                              std::string(e.what()));
            }
        }
    }

    size_t max_size_;
    std::atomic<bool> running_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
};

} // namespace StayPutVR
