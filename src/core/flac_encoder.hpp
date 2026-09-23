#pragma once
#include "core/encoder.hpp"

namespace beatdown {
class FlacEncoder : public Encoder {
public:
    std::string encode(Decoder& in, const std::filesystem::path& out, const Tags& tags,
                       const std::atomic<bool>& cancel, std::string* verbose_log) override;
};
}
