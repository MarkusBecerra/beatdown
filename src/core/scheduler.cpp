#include "core/scheduler.hpp"
#include <algorithm>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace beatdown {

size_t run_parallel(int workers, size_t count, const function<void(size_t)>& fn, const atomic<bool>& cancel) {
    if (count == 0) return 0;
    workers = std::max(1, std::min<int>(workers, static_cast<int>(count)));
    atomic<size_t> next{0}, started{0};
    atomic<bool> failed{false};
    exception_ptr captured_exception;
    mutex exception_mutex;

    auto worker = [&] {
        while (true) {
            // Check before claiming a new job
            if (cancel.load(memory_order_acquire) || failed.load(memory_order_acquire)) return;

            size_t i = next.fetch_add(1, memory_order_acq_rel);
            if (i >= count) return;

            // Double-check after claiming but before incrementing started counter
            if (cancel.load(memory_order_acquire) || failed.load(memory_order_acquire)) return;

            started.fetch_add(1, memory_order_acq_rel);

            try {
                fn(i);
            } catch (...) {
                failed.store(true, memory_order_release);
                lock_guard<mutex> lock(exception_mutex);
                if (!captured_exception) {
                    captured_exception = current_exception();
                }
                return;  // Exit immediately after exception
            }
        }
    };

    vector<thread> pool;
    try {
        for (int w = 0; w < workers; ++w) {
            pool.emplace_back(worker);
        }
    } catch (...) {
        if (pool.empty()) throw;
    }

    for (auto& t : pool) t.join();

    if (captured_exception) {
        rethrow_exception(captured_exception);
    }

    return started.load();
}

}  // namespace beatdown
