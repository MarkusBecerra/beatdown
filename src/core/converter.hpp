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
FileResult convert_one(const Job& job, const Options& o, const std::atomic<bool>& cancel);

}  // namespace beatdown
