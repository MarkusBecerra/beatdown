#pragma once
#include "core/encoder.hpp"

namespace beatdown {

// 44100/48000 kept; >48000 -> 48000; else 0 (= LAME picks its own default) (R8)
int mp3_output_rate(int source_rate);

class LameEncoder : public Encoder {
public:
    explicit LameEncoder(EncodeSettings s) : settings_(s) {}
    std::string encode(Decoder& in, const std::filesystem::path& out, const Tags& tags,
                       const std::atomic<bool>& cancel, std::string* verbose_log) override;

private:
    EncodeSettings settings_;
};

}  // namespace beatdown
