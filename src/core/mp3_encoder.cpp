#include "core/mp3_encoder.hpp"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <vector>
#include <lame/lame.h>
#include "core/unicode.hpp"

namespace beatdown {

int mp3_output_rate(int source_rate) {
    if (source_rate == 44100 || source_rate == 48000) return source_rate;
    if (source_rate > 48000) return 48000;
    return 0;
}

namespace {
void quiet_log(const char*, va_list) {}

// All text goes through UTF-16 so non-ASCII survives; LAME requires a leading BOM.
void set_text(lame_global_flags* gf, const char* id, const std::optional<std::string>& v) {
    if (!v || v->empty()) return;
    std::u16string u = u"﻿" + utf8_to_utf16(*v);
    id3tag_set_textinfo_utf16(gf, id, reinterpret_cast<const unsigned short*>(u.c_str()));
}

struct LameGuard {
    lame_global_flags* gf;
    ~LameGuard() { if (gf) lame_close(gf); }
};
}  // namespace

std::string LameEncoder::encode(Decoder& in, const std::filesystem::path& out, const Tags& tags,
                                const std::atomic<bool>& cancel, std::string* log) {
    const AudioInfo& a = in.info();
    if (a.channels > 2) return "MP3 supports mono or stereo only; source has " + std::to_string(a.channels) + " channels";

    LameGuard g{lame_init()};
    lame_global_flags* gf = g.gf;
    if (!gf) return "lame_init failed";
    lame_set_errorf(gf, quiet_log);
    lame_set_debugf(gf, quiet_log);
    lame_set_msgf(gf, quiet_log);

    lame_set_num_channels(gf, a.channels);
    lame_set_in_samplerate(gf, a.sample_rate);
    int out_rate = mp3_output_rate(a.sample_rate);
    if (out_rate) lame_set_out_samplerate(gf, out_rate);
    lame_set_mode(gf, a.channels == 1 ? MONO : JOINT_STEREO);
    lame_set_quality(gf, 0);
    if (settings_.vbr) {
        lame_set_VBR(gf, vbr_mtrh);
        lame_set_VBR_q(gf, *settings_.vbr);
    } else {
        lame_set_VBR(gf, vbr_off);
        lame_set_brate(gf, settings_.bitrate);
    }
    lame_set_bWriteVbrTag(gf, 1);

    id3tag_init(gf);
    if (!tags.empty()) {
        id3tag_add_v2(gf);
        id3tag_v2_only(gf);
        set_text(gf, "TIT2", tags.title);
        set_text(gf, "TPE1", tags.artist);
        set_text(gf, "TALB", tags.album);
        set_text(gf, "TYER", tags.date);
        set_text(gf, "TRCK", tags.track);
        set_text(gf, "TCON", tags.genre);
        if (tags.comment && !tags.comment->empty()) {
            std::u16string u = u"﻿" + utf8_to_utf16(*tags.comment);
            id3tag_set_comment_utf16(gf, nullptr, nullptr, reinterpret_cast<const unsigned short*>(u.c_str()));
        }
    } else {
        lame_set_write_id3tag_automatic(gf, 0);
    }

    if (lame_init_params(gf) < 0) return "lame_init_params rejected the settings (bitrate/sample-rate combination?)";
    if (log) {
        *log += "lame: " + std::to_string(a.sample_rate) + " Hz -> " + std::to_string(lame_get_out_samplerate(gf)) + " Hz, " +
                (settings_.vbr ? "VBR q" + std::to_string(*settings_.vbr) : "CBR " + std::to_string(settings_.bitrate)) +
                ", q0, " + (a.channels == 1 ? "mono" : "joint stereo") + "\n";
    }

    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return "cannot create " + path_to_utf8(out);

    const int64_t kFrames = 4096;
    std::vector<float> pcm(static_cast<size_t>(kFrames) * a.channels);
    std::vector<unsigned char> mp3(static_cast<size_t>(1.25 * kFrames + 7200));
    int64_t n;
    while ((n = in.read_float(pcm.data(), kFrames)) > 0) {
        if (cancel.load()) return "cancelled";
        int written = a.channels == 2
            ? lame_encode_buffer_interleaved_ieee_float(gf, pcm.data(), static_cast<int>(n), mp3.data(), static_cast<int>(mp3.size()))
            : lame_encode_buffer_ieee_float(gf, pcm.data(), pcm.data(), static_cast<int>(n), mp3.data(), static_cast<int>(mp3.size()));
        if (written < 0) return "lame encode error " + std::to_string(written);
        f.write(reinterpret_cast<const char*>(mp3.data()), written);
        if (!f) return "write failed: " + path_to_utf8(out);
    }
    int written = lame_encode_flush(gf, mp3.data(), static_cast<int>(mp3.size()));
    if (written < 0) return "lame flush error";
    f.write(reinterpret_cast<const char*>(mp3.data()), written);

    // Xing/Info frame: LAME reserved a frame at the start of the stream; fill it in now.
    size_t tag_size = lame_get_lametag_frame(gf, nullptr, 0);
    if (tag_size > 0) {
        std::vector<unsigned char> tag(tag_size);
        lame_get_lametag_frame(gf, tag.data(), tag.size());
        size_t id3_size = tags.empty() ? 0 : lame_get_id3v2_tag(gf, nullptr, 0);
        f.seekp(static_cast<std::streamoff>(id3_size));
        f.write(reinterpret_cast<const char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
    }
    f.close();
    if (!f) return "write failed: " + path_to_utf8(out);
    return "";
}

}  // namespace beatdown
