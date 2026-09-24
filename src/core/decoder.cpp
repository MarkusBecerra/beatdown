#include "core/decoder.hpp"
#include <mutex>
#include <sndfile.h>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"
#include "core/id3v2.hpp"

namespace beatdown {

static int bits_for(int format) {
    switch (format & SF_FORMAT_SUBMASK) {
        case SF_FORMAT_PCM_S8: case SF_FORMAT_PCM_U8: return 8;
        case SF_FORMAT_PCM_16: return 16;
        case SF_FORMAT_PCM_24: return 24;
        case SF_FORMAT_PCM_32: case SF_FORMAT_FLOAT: return 32;
        case SF_FORMAT_DOUBLE: return 64;
        default: return 16;
    }
}

// libsndfile hands back the tag's bytes as stored, which older Windows tools wrote as cp1252.
static std::optional<std::string> str(SNDFILE* sf, int key) {
    const char* s = sf_get_string(sf, key);
    if (!s || !*s) return std::nullopt;
    return sanitize_utf8(s);
}

std::unique_ptr<Decoder> Decoder::open(const std::filesystem::path& path, std::string& error) {
    SF_INFO info{};
    SNDFILE* sf;
    {
        // Task 18 fix round 1: libsndfile's MPEG/MP3 read backend calls mpg123_init() on every
        // open (see its own src/mpeg_decode.c, which documents this itself as a "FIXME -
        // Threading issues": that call writes shared static tables without synchronization).
        // mpg123's header claims concurrent calls "should" be safe since the tables end up with
        // the same values regardless, but that didn't hold up empirically here: a stress test of
        // 12 files at --jobs 8 crashed with SIGBUS in 9 of 60 runs before this fix, 0 of 200
        // after. Opening is a small fraction of total conversion time, so serializing just this
        // call (not the read loop that follows) keeps decoding itself fully parallel.
        static std::mutex open_mutex;
        std::lock_guard<std::mutex> lock(open_mutex);
        sf = platform::sf_open_path(path, SFM_READ, &info);
    }
    if (!sf) {
        error = sf_strerror(nullptr);
        return nullptr;
    }
    if (info.channels < 1 || info.samplerate < 1) {
        error = "no audio channels or sample rate";
        sf_close(sf);
        return nullptr;
    }
    std::unique_ptr<Decoder> d(new Decoder());
    d->sf_ = sf;
    d->path_ = path;
    d->info_.channels = info.channels;
    d->info_.sample_rate = info.samplerate;
    d->info_.frames = info.frames;
    d->info_.format = info.format;
    d->info_.bits = bits_for(info.format);
    int sub = info.format & SF_FORMAT_SUBMASK;
    d->info_.is_float = (sub == SF_FORMAT_FLOAT || sub == SF_FORMAT_DOUBLE);
    d->tags_.title = str(sf, SF_STR_TITLE);
    d->tags_.artist = str(sf, SF_STR_ARTIST);
    d->tags_.album = str(sf, SF_STR_ALBUM);
    d->tags_.date = str(sf, SF_STR_DATE);
    d->tags_.track = str(sf, SF_STR_TRACKNUMBER);
    d->tags_.genre = str(sf, SF_STR_GENRE);
    d->tags_.comment = str(sf, SF_STR_COMMENT);
    if ((info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV || (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAVEX)
        d->tags_ = merge_tags(read_wav_id3_chunk(path), d->tags_);   // id3 chunk wins: it is the Unicode-capable one
    return d;
}

Decoder::~Decoder() { if (sf_) sf_close(sf_); }

int64_t Decoder::read_float(float* out, int64_t frames) { return sf_readf_float(sf_, out, frames); }
int64_t Decoder::read_int(int32_t* out, int64_t frames) { return sf_readf_int(sf_, out, frames); }
bool Decoder::seek_start() { return sf_seek(sf_, 0, SEEK_SET) == 0; }

}  // namespace beatdown
