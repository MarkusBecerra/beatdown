#include "core/decoder.hpp"
#include <sndfile.h>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

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

static std::optional<std::string> str(SNDFILE* sf, int key) {
    const char* s = sf_get_string(sf, key);
    if (!s || !*s) return std::nullopt;
    return std::string(s);
}

std::unique_ptr<Decoder> Decoder::open(const std::filesystem::path& path, std::string& error) {
    SF_INFO info{};
    SNDFILE* sf = platform::sf_open_path(path, SFM_READ, &info);
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
    return d;
}

Decoder::~Decoder() { if (sf_) sf_close(sf_); }

int64_t Decoder::read_float(float* out, int64_t frames) { return sf_readf_float(sf_, out, frames); }
int64_t Decoder::read_int(int32_t* out, int64_t frames) { return sf_readf_int(sf_, out, frames); }
bool Decoder::seek_start() { return sf_seek(sf_, 0, SEEK_SET) == 0; }

}  // namespace beatdown
