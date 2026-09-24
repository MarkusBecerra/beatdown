#pragma once
#include <cstdint>
#include "core/options.hpp"
#include "core/std_names.hpp"

namespace beatdown {

struct Job {
    fs::path source;
    fs::path output;
    int64_t source_bytes = 0;
    string note;   // skip reason when in Plan::skipped
};

struct Plan {
    vector<Job> to_convert;
    vector<Job> skipped;
    int ignored = 0;
    int64_t total_source_bytes() const {
        int64_t total = 0;
        for (const auto& job : to_convert) total += job.source_bytes;
        return total;
    }
};

bool is_audio_input(const fs::path& path);
Plan scan(const Options& options, string& error);

}  // namespace beatdown
