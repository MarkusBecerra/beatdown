#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "core/options.hpp"

namespace beatdown {

struct Job {
    std::filesystem::path source;
    std::filesystem::path output;
    int64_t source_bytes = 0;
    std::string note;   // skip reason when in Plan::skipped
};

struct Plan {
    std::vector<Job> to_convert;
    std::vector<Job> skipped;
    int ignored = 0;
    int64_t total_source_bytes() const {
        int64_t n = 0;
        for (const auto& j : to_convert) n += j.source_bytes;
        return n;
    }
};

bool is_audio_input(const std::filesystem::path& p);
Plan scan(const Options& opts, std::string& error);

}  // namespace beatdown
