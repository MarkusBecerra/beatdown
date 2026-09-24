#pragma once
#include "core/encoder.hpp"

namespace beatdown {

// R8: 44100 and 48000 are kept; above 48000 -> 48000; below 44100 -> 44100, so the stream stays
// MPEG-1 Layer III (the only version with 320 kbps). The rare rates between the two -> 48000.
int mp3_output_rate(int source_rate);

class LameEncoder : public Encoder {
public:
    explicit LameEncoder(EncodeSettings s) : settings_(s) {}
    string encode(Decoder& in, const fs::path& out, const Tags& tags,
                  const atomic<bool>& cancel, string* verbose_log) override;

private:
    EncodeSettings settings_;
};

}  // namespace beatdown
