#pragma once
#include <atomic>
#include <cstddef>
#include <functional>

namespace beatdown {
// Runs fn(i) for i in [0, count) on up to `workers` threads. Jobs not started when `cancel` is set are never started.
// If a job throws, no further jobs are launched and the first exception is rethrown after all started jobs finish.
// Returns the number of jobs that were started. Blocks until all started jobs return.
size_t run_parallel(int workers, size_t count, const std::function<void(size_t)>& fn, const std::atomic<bool>& cancel);
}
