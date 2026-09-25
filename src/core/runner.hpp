#pragma once
#include "core/converter.hpp"
#include "core/options.hpp"
#include "core/report.hpp"
#include "core/std_names.hpp"

namespace beatdown {
using ConvertFn = function<FileResult(const Job&, const Options&, const atomic<bool>&)>;
int run(const Options& options, Reporter& reporter, atomic<bool>& cancel, ConvertFn convert = convert_one);
}  // namespace beatdown
