#pragma once
#include "core/decoder.hpp"
#include "core/options.hpp"
#include "core/std_names.hpp"

namespace beatdown {

// Post-encode verification (R17): confirms the output exists, is non-trivially sized,
// and reads back with the properties the encode was supposed to produce. Returns ""
// on success, else a human-readable reason for the mismatch.
string verify_mp3(const fs::path& out, const EncodeSettings& s, const AudioInfo& source);
string verify_flac(const fs::path& out, const AudioInfo& source);
string verify_output(const fs::path& out, const EncodeSettings& s, const AudioInfo& source);

}  // namespace beatdown
