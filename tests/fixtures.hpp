#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <sndfile.h>
#include "core/tags.hpp"

namespace fs = std::filesystem;

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
    beatdown::Tags tags;
};

fs::path make_audio(const fs::path& file, const FixtureSpec& spec = {});
void write_bytes(const fs::path& file, std::string_view bytes);
std::string read_file(const fs::path& file);

// A truncated RIFF/WAVE header (RIFF size field says 16 bytes follow, but there's no
// fmt/data chunk) so libsndfile must fail to open it with an error, not crash. Embeds
// NULs, so it must be written via this sized string_view: a plain string literal passed
// as string_view converts through strlen() and stops at the first NUL, writing only 5
// bytes ("RIFF\x10") instead of the intended 20.
inline constexpr std::string_view kCorruptWav{"RIFF\x10\x00\x00\x00WAVEjunkjunk", 20};
