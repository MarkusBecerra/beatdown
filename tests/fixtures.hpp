#pragma once
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <sndfile.h>
#include "core/tags.hpp"

// This header's own API sits at global scope (every test .cpp uses it unqualified, some without
// `using namespace beatdown;`), so it can't reach core/std_names.hpp's beatdown-scoped
// using-declarations. These two are its own equivalent, kept separately for the same reason
// std_names.hpp's own top comment gives for not being global: `namespace fs` below intentionally
// doesn't route through beatdown::fs either, for the same reason.
using std::pow;
using std::string;
using std::string_view;
using std::vector;

namespace fs = std::filesystem;

// Linear peak amplitude (1.0 = 0 dBFS) for a given dBFS level, for building test sines at a
// specific level, e.g. FixtureSpec{.amplitude = amp_for_dbfs(-20.0)}.
inline double amp_for_dbfs(double db) { return pow(10.0, db / 20.0); }

struct TempDir {
    fs::path path;
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

struct FixtureSpec {
    int container = SF_FORMAT_WAV;     // SF_FORMAT_WAV | SF_FORMAT_AIFF | SF_FORMAT_FLAC
    int subtype = SF_FORMAT_PCM_24;    // SF_FORMAT_PCM_16 | PCM_24 | PCM_32 | FLOAT
    int rate = 48000;
    int channels = 2;
    double seconds = 1.0;
    double amplitude = 0.5;  // linear peak amplitude of the test sine (1.0 = 0 dBFS)
    double freq_hz = 440.0;  // test sine frequency
    beatdown::Tags tags;
};

fs::path make_audio(const fs::path& file, const FixtureSpec& spec = {});

// The interleaved test sine make_audio and make_m4a write for `spec`.
vector<float> sine_for_test(const FixtureSpec& spec);

// An MP4-container fixture encoded with FFmpeg's own encoders and muxer, from the same sine (and
// tags) make_audio would write for `spec`; `spec.container` is ignored, and `spec.subtype` only
// picks ALAC's bit depth (SF_FORMAT_PCM_16 or SF_FORMAT_PCM_24). Aac and Alac are written the way
// iTunes writes them ('M4A ' brand); Ac3 -- a codec beatdown refuses -- as a plain MP4.
// FFmpeg's muxer writes the index ('moov') after the audio unless `moov_first`, which puts it in
// front, as iTunes does.
enum class M4aCodec { Aac, Alac, Ac3 };
fs::path make_m4a(const fs::path& file, M4aCodec codec, const FixtureSpec& spec = {}, bool moov_first = false);

// The integer samples make_m4a hands the ALAC encoder for `spec`, interleaved and left-justified
// in 32 bits: exactly what Decoder::read_int must give back, since ALAC is lossless.
vector<int32_t> alac_samples(const FixtureSpec& spec);
void write_bytes(const fs::path& file, string_view bytes);
string read_file(const fs::path& file);

// A truncated RIFF/WAVE header (RIFF size field says 16 bytes follow, but there's no
// fmt/data chunk) so libsndfile must fail to open it with an error, not crash. Embeds
// NULs, so it must be written via this sized string_view: a plain string literal passed
// as string_view converts through strlen() and stops at the first NUL, writing only 5
// bytes ("RIFF\x10") instead of the intended 20.
inline constexpr string_view kCorruptWav{"RIFF\x10\x00\x00\x00WAVEjunkjunk", 20};
