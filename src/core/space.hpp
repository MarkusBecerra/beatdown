#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include "core/decoder.hpp"
#include "core/options.hpp"

namespace beatdown {
int64_t estimate_output_bytes(const AudioInfo& a, const EncodeSettings& s);
struct SpaceCheck { int64_t needed = 0; int64_t available = 0; bool ok = true; };
SpaceCheck check_space(int64_t estimated, int64_t available);
int64_t available_bytes(const std::filesystem::path& dir, std::string& error);
}  // namespace beatdown
