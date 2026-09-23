#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace beatdown {

enum class Format { Mp3, Flac };

struct EncodeSettings {
    Format format = Format::Mp3;
    int bitrate = 320;          // CBR kbps, used when vbr is unset (R7, R27)
    std::optional<int> vbr;     // LAME VBR quality 0..9 when set (R27)
};

struct Options {
    std::filesystem::path source;
    std::filesystem::path destination;
    EncodeSettings encode;
    int jobs = 0;               // 0 = hardware concurrency (R13)
    bool overwrite = false;     // R12
    bool recursive = true;      // R1
    bool tag_from_name = false; // R9
    std::vector<std::string> strip_suffixes; // R9
    bool dry_run = false;       // R15
    bool quiet = false;         // R20
    bool verbose = false;       // R20

    int effective_jobs() const {
        if (jobs > 0) return jobs;
        unsigned n = std::thread::hardware_concurrency();
        return n == 0 ? 1 : static_cast<int>(n);
    }
    const char* output_extension() const {
        return encode.format == Format::Flac ? ".flac" : ".mp3";
    }
};

}  // namespace beatdown
