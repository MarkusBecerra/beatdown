#pragma once
#include <filesystem>
#include <string>
#include "core/decoder.hpp"
#include "core/options.hpp"

namespace beatdown {

// Post-encode verification (R17): confirms the output exists, is non-trivially sized,
// and reads back with the properties the encode was supposed to produce. Returns ""
// on success, else a human-readable reason for the mismatch.
std::string verify_mp3(const std::filesystem::path& out, const EncodeSettings& s, const AudioInfo& source);
std::string verify_flac(const std::filesystem::path& out, const AudioInfo& source);
std::string verify_output(const std::filesystem::path& out, const EncodeSettings& s, const AudioInfo& source);

}  // namespace beatdown
