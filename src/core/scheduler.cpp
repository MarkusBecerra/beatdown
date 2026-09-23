#include "core/scheduler.hpp"
#include <algorithm>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace beatdown {

size_t run_parallel(int workers, size_t count, const std::function<void(size_t)>& fn, const std::atomic<bool>& cancel) {
    if (count == 0) return 0;
    workers = std::max(1, std::min<int>(workers, static_cast<int>(count)));
    std::atomic<size_t> next{0}, started{0};
    std::atomic<bool> failed{false};
    std::exception_ptr captured_exception;
    std::mutex exception_mutex;

    auto worker = [&] {
        while (true) {
            // Check before claiming a new job
            if (cancel.load(std::memory_order_acquire) || failed.load(std::memory_order_acquire)) return;

            size_t i = next.fetch_add(1, std::memory_order_acq_rel);
            if (i >= count) return;

            // Double-check after claiming but before incrementing started counter
            if (cancel.load(std::memory_order_acquire) || failed.load(std::memory_order_acquire)) return;

            started.fetch_add(1, std::memory_order_acq_rel);

            try {
                fn(i);
            } catch (...) {
                failed.store(true, std::memory_order_release);
                std::lock_guard<std::mutex> lock(exception_mutex);
                if (!captured_exception) {
                    captured_exception = std::current_exception();
                }
                return;  // Exit immediately after exception
            }
        }
    };

    std::vector<std::thread> pool;
    try {
        for (int w = 0; w < workers; ++w) {
            pool.emplace_back(worker);
        }
    } catch (...) {
        if (pool.empty()) throw;
    }

    for (auto& t : pool) t.join();

    if (captured_exception) {
        std::rethrow_exception(captured_exception);
    }

    return started.load();
}

}  // namespace beatdown
