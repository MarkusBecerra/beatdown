#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <sndfile.h>
#include "core/tags.hpp"

namespace beatdown {

struct AudioInfo {
    int channels = 0;
    int sample_rate = 0;
    int64_t frames = 0;
    int format = 0;        // raw libsndfile format word
    int bits = 0;          // 8/16/24/32
    bool is_float = false;
    double seconds() const { return sample_rate ? double(frames) / sample_rate : 0.0; }
    int64_t pcm_bytes() const { return frames * channels * (bits / 8); }
};

class Decoder {
public:
    static std::unique_ptr<Decoder> open(const std::filesystem::path& path, std::string& error);
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    const AudioInfo& info() const { return info_; }
    const Tags& tags() const { return tags_; }
    const std::filesystem::path& path() const { return path_; }
    int64_t read_float(float* interleaved, int64_t frames);
    int64_t read_int(int32_t* interleaved, int64_t frames);
    bool seek_start();

private:
    Decoder() = default;
    SNDFILE* sf_ = nullptr;
    AudioInfo info_;
    Tags tags_;
    std::filesystem::path path_;
};

}  // namespace beatdown
