#include "core/flac_encoder.hpp"
#include <mutex>
#include <vector>
#include <sndfile.h>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

namespace beatdown {

namespace {
int64_t read_frames(Decoder& in, int32_t* buf, int64_t frames) { return in.read_int(buf, frames); }
int64_t read_frames(Decoder& in, float* buf, int64_t frames) { return in.read_float(buf, frames); }
sf_count_t write_frames(SNDFILE* sf, const int32_t* buf, sf_count_t frames) { return sf_writef_int(sf, buf, frames); }
sf_count_t write_frames(SNDFILE* sf, const float* buf, sf_count_t frames) { return sf_writef_float(sf, buf, frames); }

// Copies every frame of `in` into `sf` as `Sample`. int32_t keeps integer PCM exact (16/24-bit
// bit-exact, 32-bit truncated to 24); float sources must go through float, because libsndfile
// doesn't scale float data on an integer read (it would round the ±1.0 samples to 0/±1 — silence).
template <typename Sample>
std::string copy_frames(Decoder& in, SNDFILE* sf, const std::atomic<bool>& cancel) {
    const int64_t kFrames = 4096;
    std::vector<Sample> buf(static_cast<size_t>(kFrames) * in.info().channels);
    int64_t n;
    while ((n = read_frames(in, buf.data(), kFrames)) > 0) {
        if (cancel.load()) return "cancelled";
        if (write_frames(sf, buf.data(), n) != n) return std::string("FLAC write failed: ") + sf_strerror(sf);
    }
    return "";
}
}  // namespace

std::string FlacEncoder::encode(Decoder& in, const std::filesystem::path& out, const Tags& tags,
                                const std::atomic<bool>& cancel, std::string* log) {
    const AudioInfo& a = in.info();
    int subtype = a.bits <= 8 ? SF_FORMAT_PCM_S8 : a.bits == 16 ? SF_FORMAT_PCM_16 : SF_FORMAT_PCM_24;
    SF_INFO info{};
    info.samplerate = a.sample_rate;
    info.channels = a.channels;
    info.format = SF_FORMAT_FLAC | subtype;
    if (!sf_format_check(&info)) return "libsndfile cannot write FLAC with " + std::to_string(a.channels) + " channels at " + std::to_string(a.bits) + " bits";

    SNDFILE* sf;
    std::string open_err;
    {
        // Task 18 fix round 2: shares Decoder::open()'s lock (see sf_open_mutex()'s doc comment
        // in decoder.hpp) -- a write-mode open isn't itself the mpg123 hazard, but it still
        // touches libsndfile's global last-error code, which a concurrent read-mode open racing
        // on the same state could otherwise clobber before it's read below.
        std::lock_guard<std::mutex> lock(sf_open_mutex());
        sf = platform::sf_open_path(out, SFM_WRITE, &info);
        if (!sf) open_err = sf_strerror(nullptr);
    }
    if (!sf) return "cannot create FLAC: " + open_err;
    struct Close { SNDFILE* s; ~Close() { if (s) sf_close(s); } } closer{sf};

    double level = 1.0;  // libsndfile maps 1.0 to FLAC compression level 8
    sf_command(sf, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    sf_command(sf, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    // Vorbis comments must be UTF-8: libFLAC rejects anything else and libsndfile ignores the
    // rejection, which crashes. Tags are sanitized when read; this also covers every other source.
    auto set = [&](int key, const std::optional<std::string>& v) {
        if (v && !v->empty()) sf_set_string(sf, key, sanitize_utf8(*v).c_str());
    };
    set(SF_STR_TITLE, tags.title);
    set(SF_STR_ARTIST, tags.artist);
    set(SF_STR_ALBUM, tags.album);
    set(SF_STR_DATE, tags.date);
    set(SF_STR_TRACKNUMBER, tags.track);
    set(SF_STR_GENRE, tags.genre);
    set(SF_STR_COMMENT, tags.comment);
    if (log) *log += "flac: level 8, " + std::to_string(a.sample_rate) + " Hz, " + std::to_string(a.bits > 24 ? 24 : a.bits) + "-bit\n";

    std::string e = a.is_float ? copy_frames<float>(in, sf, cancel) : copy_frames<int32_t>(in, sf, cancel);
    if (!e.empty()) return e;
    closer.s = nullptr;
    if (int rc = sf_close(sf); rc != 0) return std::string("FLAC finalize failed: ") + sf_error_number(rc);
    return "";
}

}  // namespace beatdown
