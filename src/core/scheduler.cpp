#include "core/scheduler.hpp"
#include <algorithm>
#include <thread>
#include <vector>

namespace beatdown {

size_t run_parallel(int workers, size_t count, const std::function<void(size_t)>& fn, const std::atomic<bool>& cancel) {
    if (count == 0) return 0;
    workers = std::max(1, std::min<int>(workers, static_cast<int>(count)));
    std::atomic<size_t> next{0}, started{0};
    auto worker = [&] {
        for (;;) {
            if (cancel.load()) return;
            size_t i = next.fetch_add(1);
            if (i >= count) return;
            started.fetch_add(1);
            fn(i);
        }
    };
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    return started.load();
}

}  // namespace beatdown
