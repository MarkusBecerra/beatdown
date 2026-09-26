#pragma once
#include <cstdint>
#include <sndfile.h>
#include "core/std_names.hpp"
#include "core/tags.hpp"

namespace beatdown {

struct AudioInfo {
    int channels = 0;
    int sample_rate = 0;
    // For an M4A source, the container's declared length until the stream has been read to its
    // end, then the exact number of frames it decoded to (see M4aDecoder in m4a_decoder.cpp).
    int64_t frames = 0;
    int format = 0;        // raw libsndfile format word; 0 for an M4A source
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

// A source file being decoded. Two backends sit behind this interface (PRD Q16): libsndfile for
// WAV/AIFF/FLAC (and the MP3s that are refused), and FFmpeg's libraries for M4A (AAC or ALAC in
// an MP4 container). Callers only ever see this class.
class Decoder {
public:
    // Picks the backend by content, not extension: a file that opens with an MP4 'ftyp' box goes
    // to FFmpeg, everything else to libsndfile -- the same way an MP3 renamed to .wav is still
    // recognised as MP3. On failure returns nullptr and sets `error`.
    static unique_ptr<Decoder> open(const fs::path& path, string& error);
    virtual ~Decoder() = default;
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    const AudioInfo& info() const { return info_; }
    const Tags& tags() const { return tags_; }
    const fs::path& path() const { return path_; }
    // Interleaved frames, libsndfile's scaling for both backends: floats nominally within +-1.0,
    // integers left-justified in 32 bits (a 16-bit sample x reads back as x << 16). Returns the
    // number of frames read; 0 at the end of the stream, or after a decode error (read_error()).
    virtual int64_t read_float(float* interleaved, int64_t frames) = 0;
    virtual int64_t read_int(int32_t* interleaved, int64_t frames) = 0;
    virtual bool seek_start() = 0;
    // "" unless reading stopped early because the source couldn't be decoded to its end (a
    // damaged or truncated M4A) -- a read that returns 0 then means "gave up", not "finished".
    // libsndfile doesn't distinguish the two, so this is always "" for its formats.
    const string& read_error() const { return read_error_; }

protected:
    Decoder() = default;
    AudioInfo info_;
    Tags tags_;
    fs::path path_;
    string read_error_;
};

}  // namespace beatdown
