#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include "core/decoder.hpp"
#include "core/options.hpp"
#include "core/scanner.hpp"
#include "core/tags.hpp"

namespace beatdown {

enum class Outcome { Converted, Failed, Cancelled };

struct FileResult {
    Job job;
    Outcome outcome = Outcome::Failed;
    std::string error;
    int64_t output_bytes = 0;
    std::chrono::milliseconds elapsed{0};
    bool disk_full = false;
    std::string verbose_log;
};

std::filesystem::path temp_path_for(const std::filesystem::path& output);
Tags resolve_tags(const Decoder& d, const Options& o);

// True if `available` bytes of free space can't cover `estimated` output bytes, or if
// `error` looks like an OS out-of-space message. `available`/`estimated` are in bytes;
// pass a very large `available` when free space couldn't be determined, so only the
// error-text check applies.
bool looks_like_disk_full(const std::string& error, int64_t available, int64_t estimated);

FileResult convert_one(const Job& job, const Options& o, const std::atomic<bool>& cancel);

}  // namespace beatdown
