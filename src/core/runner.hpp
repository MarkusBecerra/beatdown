#pragma once
#include <atomic>
#include <functional>
#include "core/converter.hpp"
#include "core/options.hpp"
#include "core/report.hpp"

namespace beatdown {
using ConvertFn = std::function<FileResult(const Job&, const Options&, const std::atomic<bool>&)>;
int run(const Options& o, Reporter& rep, std::atomic<bool>& cancel, ConvertFn convert = convert_one);
}  // namespace beatdown
