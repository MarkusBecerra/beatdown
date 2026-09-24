#pragma once
#include <cstddef>
#include "core/std_names.hpp"

namespace beatdown {
// Runs fn(i) for i in [0, count) on up to `workers` threads. Jobs not started when `cancel` is set are never started.
// If a job throws, no further jobs are launched and the first exception is rethrown after all started jobs finish.
// Returns the number of jobs that were started. Blocks until all started jobs return.
size_t run_parallel(int workers, size_t count, const function<void(size_t)>& fn, const atomic<bool>& cancel);
}
