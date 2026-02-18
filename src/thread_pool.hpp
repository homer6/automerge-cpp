#pragma once

// Internal header — not installed.
// Minimal std::jthread-based thread pool with parallel_for.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

namespace automerge_cpp::detail {

class ThreadPool {
public:
    explicit ThreadPool(unsigned int num_threads)
        : num_threads_{num_threads} {
        workers_.reserve(num_threads);
        for (unsigned int i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
        }
    }

    ~ThreadPool() {
        {
            auto lock = std::scoped_lock{mutex_};
            stop_.store(true, std::memory_order_relaxed);
        }
        cv_.notify_all();
        // std::jthread destructor requests stop and joins automatically
    }

    ThreadPool(const ThreadPool&) = delete;
    auto operator=(const ThreadPool&) -> ThreadPool& = delete;
    ThreadPool(ThreadPool&&) = delete;
    auto operator=(ThreadPool&&) -> ThreadPool& = delete;

    /// Partition [0, count) into chunks and dispatch across workers.
    /// Blocks until all chunks complete. fn is called as fn(index) for
    /// each index in [0, count).
    template <typename Fn>
    void parallel_for(std::size_t count, Fn&& fn) {
        if (count == 0) return;

        auto chunks = std::min(static_cast<std::size_t>(num_threads_), count);
        auto done = std::latch{static_cast<std::ptrdiff_t>(chunks)};

        for (std::size_t c = 0; c < chunks; ++c) {
            auto begin = c * count / chunks;
            auto end = (c + 1) * count / chunks;
            submit([&fn, &done, begin, end]() {
                for (auto i = begin; i < end; ++i) {
                    fn(i);
                }
                done.count_down();
            });
        }

        done.wait();
    }

    auto size() const -> unsigned int { return num_threads_; }

private:
    void submit(std::function<void()> task) {
        {
            auto lock = std::scoped_lock{mutex_};
            tasks_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

    void worker_loop(std::stop_token st) {
        while (true) {
            auto task = std::function<void()>{};
            {
                auto lock = std::unique_lock{mutex_};
                cv_.wait(lock, [&] {
                    return !tasks_.empty() || stop_.load(std::memory_order_relaxed) || st.stop_requested();
                });
                if ((stop_.load(std::memory_order_relaxed) || st.stop_requested()) && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    unsigned int num_threads_;
    std::vector<std::jthread> workers_;
    std::deque<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};

}  // namespace automerge_cpp::detail
