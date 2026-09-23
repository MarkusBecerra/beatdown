#include "core/flac_encoder.hpp"

namespace beatdown {

// Replaced in Task 9 with a real libsndfile-based FLAC writer.
std::string FlacEncoder::encode(Decoder&, const std::filesystem::path&, const Tags&,
                                const std::atomic<bool>&, std::string*) {
    return "not implemented";
}

}  // namespace beatdown
