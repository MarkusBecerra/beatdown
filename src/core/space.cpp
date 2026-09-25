#include "core/space.hpp"
#include "core/unicode.hpp"

namespace beatdown {

int64_t estimate_output_bytes(const AudioInfo& info, const EncodeSettings& settings) {
    if (settings.format == Format::Flac) return static_cast<int64_t>(0.7 * static_cast<double>(info.pcm_bytes()));
    int kbps = settings.vbr ? 256 : settings.bitrate;   // V0 averages ~245 kbps on dense material; 256 is a safe bound
    return static_cast<int64_t>(info.seconds() * kbps * 125.0);
}

SpaceCheck check_space(int64_t estimated, int64_t available) {
    SpaceCheck check;
    check.needed = estimated + estimated / 10;
    check.available = available;
    check.ok = available >= check.needed;
    return check;
}

int64_t available_bytes(const fs::path& dir, string& error) {
    error_code fs_error;
    auto space_info = fs::space(dir, fs_error);
    if (fs_error) { error = "cannot read free space of " + path_to_utf8(dir) + ": " + fs_error.message(); return 0; }
    return static_cast<int64_t>(space_info.available);
}

}  // namespace beatdown
