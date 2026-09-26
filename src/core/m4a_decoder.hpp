#pragma once
#include "core/decoder.hpp"
#include "core/std_names.hpp"

namespace beatdown {

// True if `path` starts with an ISO base media file's 'ftyp' box -- M4A, MP4 and their kin.
bool looks_like_mp4(const fs::path& path);

// Opens an MP4-container file whose audio is AAC or ALAC, decoded through FFmpeg's libraries.
// Anything else (another codec, no audio track, a damaged file) returns nullptr with `error` set.
unique_ptr<Decoder> open_m4a(const fs::path& path, string& error);

}  // namespace beatdown
