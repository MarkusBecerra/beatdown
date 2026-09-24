#include "core/flac_encoder.hpp"
#include <mutex>
#include <vector>
#include <sndfile.h>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

namespace beatdown {

namespace {
int64_t read_frames(Decoder& in, int32_t* buffer, int64_t frames) { return in.read_int(buffer, frames); }
int64_t read_frames(Decoder& in, float* buffer, int64_t frames) { return in.read_float(buffer, frames); }
sf_count_t write_frames(SNDFILE* sndfile, const int32_t* buffer, sf_count_t frames) { return sf_writef_int(sndfile, buffer, frames); }
sf_count_t write_frames(SNDFILE* sndfile, const float* buffer, sf_count_t frames) { return sf_writef_float(sndfile, buffer, frames); }

// Copies every frame of `in` into `sndfile` as `Sample`. int32_t keeps integer PCM exact (16/24-bit
// bit-exact, 32-bit truncated to 24); float sources must go through float, because libsndfile
// doesn't scale float data on an integer read (it would round the ±1.0 samples to 0/±1 — silence).
template <typename Sample>
string copy_frames(Decoder& in, SNDFILE* sndfile, const atomic<bool>& cancel) {
    const int64_t kFrames = 4096;
    vector<Sample> buffer(static_cast<size_t>(kFrames) * in.info().channels);
    int64_t frames_read;
    while ((frames_read = read_frames(in, buffer.data(), kFrames)) > 0) {
        if (cancel.load()) return "cancelled";
        if (write_frames(sndfile, buffer.data(), frames_read) != frames_read) return string("FLAC write failed: ") + sf_strerror(sndfile);
    }
    return "";
}
}  // namespace

string FlacEncoder::encode(Decoder& in, const fs::path& out, const Tags& tags,
                           const atomic<bool>& cancel, string* log) {
    const AudioInfo& source_info = in.info();
    int subtype = source_info.bits <= 8 ? SF_FORMAT_PCM_S8 : source_info.bits == 16 ? SF_FORMAT_PCM_16 : SF_FORMAT_PCM_24;
    SF_INFO info{};
    info.samplerate = source_info.sample_rate;
    info.channels = source_info.channels;
    info.format = SF_FORMAT_FLAC | subtype;
    if (!sf_format_check(&info)) return "libsndfile cannot write FLAC with " + to_string(source_info.channels) + " channels at " + to_string(source_info.bits) + " bits";

    SNDFILE* sndfile;
    string open_err;
    {
        // Task 18 fix round 2: shares Decoder::open()'s lock (see sf_open_mutex()'s doc comment
        // in decoder.hpp) -- a write-mode open isn't itself the mpg123 hazard, but it still
        // touches libsndfile's global last-error code, which a concurrent read-mode open racing
        // on the same state could otherwise clobber before it's read below.
        lock_guard<mutex> lock(sf_open_mutex());
        sndfile = platform::sf_open_path(out, SFM_WRITE, &info);
        if (!sndfile) open_err = sf_strerror(nullptr);
    }
    if (!sndfile) return "cannot create FLAC: " + open_err;
    struct Close { SNDFILE* handle; ~Close() { if (handle) sf_close(handle); } } closer{sndfile};

    double level = 1.0;  // libsndfile maps 1.0 to FLAC compression level 8
    sf_command(sndfile, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    sf_command(sndfile, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    // Vorbis comments must be UTF-8: libFLAC rejects anything else and libsndfile ignores the
    // rejection, which crashes. Tags are sanitized when read; this also covers every other source.
    auto set = [&](int key, const optional<string>& value) {
        if (value && !value->empty()) sf_set_string(sndfile, key, sanitize_utf8(*value).c_str());
    };
    set(SF_STR_TITLE, tags.title);
    set(SF_STR_ARTIST, tags.artist);
    set(SF_STR_ALBUM, tags.album);
    set(SF_STR_DATE, tags.date);
    set(SF_STR_TRACKNUMBER, tags.track);
    set(SF_STR_GENRE, tags.genre);
    set(SF_STR_COMMENT, tags.comment);
    if (log) *log += "flac: level 8, " + to_string(source_info.sample_rate) + " Hz, " + to_string(source_info.bits > 24 ? 24 : source_info.bits) + "-bit\n";

    string copy_error = source_info.is_float ? copy_frames<float>(in, sndfile, cancel) : copy_frames<int32_t>(in, sndfile, cancel);
    if (!copy_error.empty()) return copy_error;
    closer.handle = nullptr;
    if (int rc = sf_close(sndfile); rc != 0) return string("FLAC finalize failed: ") + sf_error_number(rc);
    return "";
}

}  // namespace beatdown
