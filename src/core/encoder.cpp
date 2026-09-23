#include "core/encoder.hpp"
#include "core/flac_encoder.hpp"
#include "core/mp3_encoder.hpp"

namespace beatdown {

std::unique_ptr<Encoder> make_encoder(const EncodeSettings& s) {
    if (s.format == Format::Flac) return std::make_unique<FlacEncoder>();
    return std::make_unique<LameEncoder>(s);
}

}  // namespace beatdown
