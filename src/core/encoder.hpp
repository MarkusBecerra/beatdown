#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include "core/decoder.hpp"
#include "core/options.hpp"
#include "core/tags.hpp"

namespace beatdown {

class Encoder {
public:
    virtual ~Encoder() = default;
    // Encodes all of `in` into `out` (created/truncated). Returns "" on success, "cancelled" if
    // `cancel` became true, else an error message. Never renames; caller owns temp/rename.
    virtual std::string encode(Decoder& in, const std::filesystem::path& out, const Tags& tags,
                               const std::atomic<bool>& cancel, std::string* verbose_log) = 0;

    // Task 18: the peak sample level (dBFS) LAME measured while decoding its own just-encoded
    // MP3 on the fly, valid after a successful encode() — nullopt for formats with no decoded-peak
    // concept (FLAC is bit-exact, so its peak is whatever the source's already is).
    virtual std::optional<double> decoded_peak_dbfs() const { return std::nullopt; }
};

// Mp3 -> LameEncoder; Flac -> FlacEncoder (Task 9)
std::unique_ptr<Encoder> make_encoder(const EncodeSettings& s);

}  // namespace beatdown
