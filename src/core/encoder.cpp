#include "core/encoder.hpp"
#include "core/flac_encoder.hpp"
#include "core/mp3_encoder.hpp"

namespace beatdown {

unique_ptr<Encoder> make_encoder(const EncodeSettings& settings) {
    if (settings.format == Format::Flac) return make_unique<FlacEncoder>();
    return make_unique<LameEncoder>(settings);
}

}  // namespace beatdown
