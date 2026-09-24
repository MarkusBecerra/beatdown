#pragma once
#include "core/options.hpp"
#include "core/std_names.hpp"

namespace beatdown {

struct ContentCheckResult {
    // "" when the output's audio matches the source, else a message starting with "audio content".
    string error;
    // Task 18 fix round 1: the decoded peak (dBFS, max |sample| of the unclipped float MP3
    // decode) -- set only for MP3 (nullopt for FLAC, which has no decoded-peak concept).
    optional<double> peak_dbfs;
};

// Verifies `out`'s decoded audio content against `source`, re-opened with Decoder. FLAC:
// bit-for-bit (within format-appropriate tolerance) comparison. MP3: level comparison after a
// shared low-pass (removes the encoder's own ~20.3 kHz rolloff from the picture) plus a decoded
// length check.
ContentCheckResult verify_content(const fs::path& out, const EncodeSettings& settings, const fs::path& source);

}  // namespace beatdown
