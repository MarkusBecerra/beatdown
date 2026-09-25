#pragma once
#include <cstdint>
#include <set>
#include "core/std_names.hpp"
#include "core/tags.hpp"

namespace beatdown {

struct Mp3Info {
    int sample_rate = 0;
    int channels = 0;
    int64_t audio_frames = 0;       // excludes the Xing/Info frame
    int samples_per_frame = 0;
    std::set<int> bitrates;         // kbps values seen in audio frames (std::set: `set` names a
                                     // local lambda elsewhere in this project; kept qualified here
                                     // rather than shadow it via a using-declaration)
    bool has_xing = false;
    size_t id3v2_size = 0;
    size_t trailing_bytes = 0;      // bytes after the last parsable frame
    Tags tags;

    bool cbr(int kbps) const { return bitrates.size() == 1 && *bitrates.begin() == kbps; }
    double duration_seconds() const { return sample_rate ? double(audio_frames) * samples_per_frame / sample_rate : 0.0; }
};

bool parse_mp3(const fs::path& file, Mp3Info& out, string& error);
string build_mp3_frame_for_test(int kbps, int sample_rate, bool padding, bool mono, const char* xing_tag, bool crc = false);

}  // namespace beatdown
