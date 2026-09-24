#pragma once
#include <cstdint>
#include <sndfile.h>
#include "core/std_names.hpp"
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
    // Task 18 fix round 2: a source already coded as MPEG (MP3) -- re-encoding lossy audio is
    // refused rather than silently done, so this is checked before any encode is attempted.
    bool is_mpeg() const { return (format & SF_FORMAT_TYPEMASK) == SF_FORMAT_MPEG; }
};

// Task 18 fix round 2: guards every libsndfile sf_open call, read or write, process-wide. The
// real race (round-1 review re-review) is inside mpg123_new(), not mpg123_init(): mpg123 1.33's
// ARM CPU-feature probe uses a static sigjmp_buf and temporarily swaps the process-wide SIGILL
// handler while it runs (mpg123_init() itself is a no-op on this build, see mpg123.h). Also
// covers non-MPEG opens: libsndfile resets its own global last-error code on every sf_open
// regardless of mode, so a write-mode open (FlacEncoder) racing a read-mode one can read back the
// wrong error string. A single mutex shared by both call sites (Decoder::open here,
// FlacEncoder's write-mode open) keeps that global state consistent.
mutex& sf_open_mutex();

class Decoder {
public:
    static unique_ptr<Decoder> open(const fs::path& path, string& error);
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    const AudioInfo& info() const { return info_; }
    const Tags& tags() const { return tags_; }
    const fs::path& path() const { return path_; }
    int64_t read_float(float* interleaved, int64_t frames);
    int64_t read_int(int32_t* interleaved, int64_t frames);
    bool seek_start();

private:
    Decoder() = default;
    SNDFILE* sf_ = nullptr;
    AudioInfo info_;
    Tags tags_;
    fs::path path_;
};

}  // namespace beatdown
