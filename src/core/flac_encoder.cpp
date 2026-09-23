#include "core/flac_encoder.hpp"
#include <vector>
#include <sndfile.h>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

namespace beatdown {

std::string FlacEncoder::encode(Decoder& in, const std::filesystem::path& out, const Tags& tags,
                                const std::atomic<bool>& cancel, std::string* log) {
    const AudioInfo& a = in.info();
    int subtype = a.bits <= 8 ? SF_FORMAT_PCM_S8 : a.bits == 16 ? SF_FORMAT_PCM_16 : SF_FORMAT_PCM_24;
    SF_INFO info{};
    info.samplerate = a.sample_rate;
    info.channels = a.channels;
    info.format = SF_FORMAT_FLAC | subtype;
    if (!sf_format_check(&info)) return "libsndfile cannot write FLAC with " + std::to_string(a.channels) + " channels at " + std::to_string(a.bits) + " bits";

    SNDFILE* sf = platform::sf_open_path(out, SFM_WRITE, &info);
    if (!sf) return std::string("cannot create FLAC: ") + sf_strerror(nullptr);
    struct Close { SNDFILE* s; ~Close() { if (s) sf_close(s); } } closer{sf};

    double level = 1.0;  // libsndfile maps 1.0 to FLAC compression level 8
    sf_command(sf, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    sf_command(sf, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    auto set = [&](int key, const std::optional<std::string>& v) { if (v && !v->empty()) sf_set_string(sf, key, v->c_str()); };
    set(SF_STR_TITLE, tags.title);
    set(SF_STR_ARTIST, tags.artist);
    set(SF_STR_ALBUM, tags.album);
    set(SF_STR_DATE, tags.date);
    set(SF_STR_TRACKNUMBER, tags.track);
    set(SF_STR_GENRE, tags.genre);
    set(SF_STR_COMMENT, tags.comment);
    if (log) *log += "flac: level 8, " + std::to_string(a.sample_rate) + " Hz, " + std::to_string(a.bits > 24 ? 24 : a.bits) + "-bit\n";

    const int64_t kFrames = 4096;
    std::vector<int32_t> buf(static_cast<size_t>(kFrames) * a.channels);
    int64_t n;
    while ((n = in.read_int(buf.data(), kFrames)) > 0) {
        if (cancel.load()) return "cancelled";
        if (sf_writef_int(sf, buf.data(), n) != n) return std::string("FLAC write failed: ") + sf_strerror(sf);
    }
    sf_close(sf);
    closer.s = nullptr;
    return "";
}

}  // namespace beatdown
