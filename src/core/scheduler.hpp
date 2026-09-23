#pragma once
#include <atomic>
#include <cstddef>
#include <functional>

namespace beatdown {
size_t run_parallel(int workers, size_t count, const std::function<void(size_t)>& fn, const std::atomic<bool>& cancel);
}
