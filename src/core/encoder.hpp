#pragma once
#include "core/decoder.hpp"
#include "core/options.hpp"
#include "core/std_names.hpp"
#include "core/tags.hpp"

namespace beatdown {

class Encoder {
public:
    virtual ~Encoder() = default;
    // Encodes all of `in` into `out` (created/truncated). Returns "" on success, "cancelled" if
    // `cancel` became true, else an error message. Never renames; caller owns temp/rename.
    virtual string encode(Decoder& in, const fs::path& out, const Tags& tags,
                          const atomic<bool>& cancel, string* verbose_log) = 0;
};

// Mp3 -> LameEncoder; Flac -> FlacEncoder (Task 9)
unique_ptr<Encoder> make_encoder(const EncodeSettings& s);

}  // namespace beatdown
