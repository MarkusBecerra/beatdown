#pragma once
#include <filesystem>
#include <string>
#include "core/options.hpp"

namespace beatdown {

// Task 18: verifies an output's decoded audio content against its source, not just the
// container metadata verify_output already checks. Re-opens `source` with Decoder. Returns ""
// when the content matches within tolerance, else a message starting with "audio content".
std::string verify_content(const std::filesystem::path& out, const EncodeSettings& s, const std::filesystem::path& source);

}  // namespace beatdown
