#pragma once
#include <cstdint>
#include "core/decoder.hpp"
#include "core/options.hpp"
#include "core/std_names.hpp"

namespace beatdown {
int64_t estimate_output_bytes(const AudioInfo& info, const EncodeSettings& settings);
struct SpaceCheck { int64_t needed = 0; int64_t available = 0; bool ok = true; };
SpaceCheck check_space(int64_t estimated, int64_t available);
int64_t available_bytes(const fs::path& dir, string& error);
}  // namespace beatdown
