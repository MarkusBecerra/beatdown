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
static optional<string> read_sndfile_string(SNDFILE* sndfile, int key) {
    const char* value = sf_get_string(sndfile, key);
    if (!value || !*value) return nullopt;
    return sanitize_utf8(value);
}

mutex& sf_open_mutex() {
    static mutex mutex_instance;
    return mutex_instance;
}

unique_ptr<Decoder> Decoder::open(const fs::path& path, string& error) {
    SF_INFO info{};
    SNDFILE* sndfile;
    {
        // See sf_open_mutex()'s doc comment (decoder.hpp): the race is in mpg123_new(), so this
        // lock must stay around every read-mode open (not just MP3s -- opening one is what can
        // trigger it, and there's no cheap way to know a file is MPEG before opening it). The
        // error string is read here too, still inside the lock: it comes from libsndfile's
        // global last-error code, which a concurrent open (in either mode) can overwrite first.
        lock_guard<mutex> lock(sf_open_mutex());
        sndfile = platform::sf_open_path(path, SFM_READ, &info);
        if (!sndfile) error = sf_strerror(nullptr);
    }
    if (!sndfile) return nullptr;
    if (info.channels < 1 || info.samplerate < 1) {
        error = "no audio channels or sample rate";
        sf_close(sndfile);
        return nullptr;
    }
    unique_ptr<Decoder> decoder(new Decoder());
    decoder->sndfile_ = sndfile;
    decoder->path_ = path;
    decoder->info_.channels = info.channels;
    decoder->info_.sample_rate = info.samplerate;
    decoder->info_.frames = info.frames;
    decoder->info_.format = info.format;
    decoder->info_.bits = bits_for(info.format);
    int subtype = info.format & SF_FORMAT_SUBMASK;
    decoder->info_.is_float = (subtype == SF_FORMAT_FLOAT || subtype == SF_FORMAT_DOUBLE);
    decoder->tags_.title = read_sndfile_string(sndfile, SF_STR_TITLE);
    decoder->tags_.artist = read_sndfile_string(sndfile, SF_STR_ARTIST);
    decoder->tags_.album = read_sndfile_string(sndfile, SF_STR_ALBUM);
    decoder->tags_.date = read_sndfile_string(sndfile, SF_STR_DATE);
    decoder->tags_.track = read_sndfile_string(sndfile, SF_STR_TRACKNUMBER);
    decoder->tags_.genre = read_sndfile_string(sndfile, SF_STR_GENRE);
    decoder->tags_.comment = read_sndfile_string(sndfile, SF_STR_COMMENT);
    if ((info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV || (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAVEX)
        decoder->tags_ = merge_tags(read_wav_id3_chunk(path), decoder->tags_);   // id3 chunk wins: it is the Unicode-capable one
    return decoder;
}

Decoder::~Decoder() { if (sndfile_) sf_close(sndfile_); }

int64_t Decoder::read_float(float* out, int64_t frames) { return sf_readf_float(sndfile_, out, frames); }
int64_t Decoder::read_int(int32_t* out, int64_t frames) { return sf_readf_int(sndfile_, out, frames); }
bool Decoder::seek_start() { return sf_seek(sndfile_, 0, SEEK_SET) == 0; }

}  // namespace beatdown
