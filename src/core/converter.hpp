#pragma once
#include <cstdint>
#include "core/decoder.hpp"
#include "core/options.hpp"
#include "core/scanner.hpp"
#include "core/std_names.hpp"
#include "core/tags.hpp"

namespace beatdown {

enum class Outcome { Converted, Failed, Cancelled };

struct FileResult {
    Job job;
    Outcome outcome = Outcome::Failed;
    string error;
    int64_t output_bytes = 0;
    chrono::milliseconds elapsed{0};
    bool disk_full = false;
    string verbose_log;
    // Task 18: the decoded peak (dBFS) of an MP3 output, from verify_content's own decode;
    // nullopt for FLAC (bit-exact, no decoded-peak concept) or a failed/cancelled conversion.
    optional<double> peak_dbfs;
};

// A temp path in `output`'s folder, ".beatdown-<output filename>.<8 random hex>.part", with fresh
// random digits on every call, so two jobs don't write one temp file even if they share an output.
fs::path temp_path_for(const fs::path& output);
Tags resolve_tags(const Decoder& d, const Options& o);

// True if `available` bytes of free space can't cover `estimated` output bytes, or if
// `error` looks like an OS out-of-space message. `available`/`estimated` are in bytes;
// pass a very large `available` when free space couldn't be determined, so only the
// error-text check applies.
bool looks_like_disk_full(const string& error, int64_t available, int64_t estimated);

FileResult convert_one(const Job& job, const Options& o, const atomic<bool>& cancel);

}  // namespace beatdown
