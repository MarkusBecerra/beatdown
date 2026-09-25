#pragma once
#include "core/encoder.hpp"

namespace beatdown {
class FlacEncoder : public Encoder {
public:
    string encode(Decoder& in, const fs::path& out, const Tags& tags,
                  const atomic<bool>& cancel, string* verbose_log) override;
};
}  // namespace beatdown
