#include "core/space.hpp"
#include "core/unicode.hpp"

namespace beatdown {

int64_t estimate_output_bytes(const AudioInfo& a, const EncodeSettings& s) {
    if (s.format == Format::Flac) return static_cast<int64_t>(0.7 * static_cast<double>(a.pcm_bytes()));
    int kbps = s.vbr ? 256 : s.bitrate;   // V0 averages ~245 kbps on dense material; 256 is a safe bound
    return static_cast<int64_t>(a.seconds() * kbps * 125.0);
}

SpaceCheck check_space(int64_t estimated, int64_t available) {
    SpaceCheck c;
    c.needed = estimated + estimated / 10;
    c.available = available;
    c.ok = available >= c.needed;
    return c;
}

int64_t available_bytes(const fs::path& dir, string& error) {
    error_code ec;
    auto sp = fs::space(dir, ec);
    if (ec) { error = "cannot read free space of " + path_to_utf8(dir) + ": " + ec.message(); return 0; }
    return static_cast<int64_t>(sp.available);
}

}  // namespace beatdown
