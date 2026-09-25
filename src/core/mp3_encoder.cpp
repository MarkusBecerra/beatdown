#include "core/mp3_encoder.hpp"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <vector>
#include <lame/lame.h>
#include "core/unicode.hpp"

namespace beatdown {

int mp3_output_rate(int source_rate) {
    return source_rate <= 44100 ? 44100 : 48000;
}

namespace {
void quiet_log(const char*, va_list) {}

// All text goes through UTF-16 so non-ASCII survives; LAME's *_utf16 setters require a leading
// byte-order mark, built numerically here so no editor can silently strip it. Sanitizing again
// covers tags from any source, filename-derived ones included.
u16string utf16_with_bom(const string& utf8) {
    u16string units(1, char16_t(0xFEFF));
    units += utf8_to_utf16(sanitize_utf8(utf8));
    return units;
}

void set_text(lame_global_flags* lame, const char* id, const optional<string>& value) {
    if (!value || value->empty()) return;
    u16string units = utf16_with_bom(*value);
    id3tag_set_textinfo_utf16(lame, id, reinterpret_cast<const unsigned short*>(units.c_str()));
}

struct LameGuard {
    lame_global_flags* handle;
    ~LameGuard() { if (handle) lame_close(handle); }
};
}  // namespace

string LameEncoder::encode(Decoder& in, const fs::path& out, const Tags& tags,
                           const atomic<bool>& cancel, string* log) {
    const AudioInfo& source_info = in.info();
    if (source_info.channels > 2) return "MP3 supports mono or stereo only; source has " + to_string(source_info.channels) + " channels";

    LameGuard guard{lame_init()};
    lame_global_flags* lame = guard.handle;
    if (!lame) return "lame_init failed";
    lame_set_errorf(lame, quiet_log);
    lame_set_debugf(lame, quiet_log);
    lame_set_msgf(lame, quiet_log);

    lame_set_num_channels(lame, source_info.channels);
    lame_set_in_samplerate(lame, source_info.sample_rate);
    const int out_rate = mp3_output_rate(source_info.sample_rate);
    lame_set_out_samplerate(lame, out_rate);
    lame_set_mode(lame, source_info.channels == 1 ? MONO : JOINT_STEREO);
    lame_set_quality(lame, 0);
    if (settings_.vbr) {
        lame_set_VBR(lame, vbr_mtrh);
        lame_set_VBR_q(lame, *settings_.vbr);
    } else {
        lame_set_VBR(lame, vbr_off);
        lame_set_brate(lame, settings_.bitrate);
    }
    lame_set_bWriteVbrTag(lame, 1);

    id3tag_init(lame);
    if (!tags.empty()) {
        id3tag_add_v2(lame);
        id3tag_v2_only(lame);
        set_text(lame, "TIT2", tags.title);
        set_text(lame, "TPE1", tags.artist);
        set_text(lame, "TALB", tags.album);
        set_text(lame, "TYER", tags.date);
        set_text(lame, "TRCK", tags.track);
        set_text(lame, "TCON", tags.genre);
        if (tags.comment && !tags.comment->empty()) {
            u16string units = utf16_with_bom(*tags.comment);
            id3tag_set_comment_utf16(lame, nullptr, nullptr, reinterpret_cast<const unsigned short*>(units.c_str()));
        }
    } else {
        lame_set_write_id3tag_automatic(lame, 0);
    }

    if (lame_init_params(lame) < 0) return "lame_init_params rejected the settings (bitrate/sample-rate combination?)";
    // LAME silently substitutes what it can't honour (the nearest legal bitrate, another output
    // rate); refuse here, before anything is written, instead of failing verification after a full encode.
    if (!settings_.vbr && lame_get_brate(lame) != settings_.bitrate)
        return "LAME can't encode CBR " + to_string(settings_.bitrate) + " kbps at " + to_string(out_rate) +
               " Hz (it would use " + to_string(lame_get_brate(lame)) + " kbps)";
    if (lame_get_out_samplerate(lame) != out_rate)
        return "LAME would write " + to_string(lame_get_out_samplerate(lame)) + " Hz instead of " + to_string(out_rate) + " Hz";
    if (log) {
        *log += "lame: " + to_string(source_info.sample_rate) + " Hz -> " + to_string(lame_get_out_samplerate(lame)) + " Hz, " +
                (settings_.vbr ? "VBR q" + to_string(*settings_.vbr) : "CBR " + to_string(settings_.bitrate)) +
                ", q0, " + (source_info.channels == 1 ? "mono" : "joint stereo") + "\n";
    }

    ofstream file(out, ios::binary | ios::trunc);
    if (!file) return "cannot create " + path_to_utf8(out);

    const int64_t kFrames = 4096;
    vector<float> pcm(static_cast<size_t>(kFrames) * source_info.channels);
    // Task 18 fix round 2: LAME's own sizing guidance is mp3buf_size = 1.25*num_samples + 7200,
    // where num_samples must reflect the OUTPUT sample count for this many input frames, not the
    // input frame count itself. For an upsampled source (e.g. 8 kHz -> 44.1 kHz, R7/R8) the same
    // kFrames of input covers far more encoded output time than kFrames alone suggests -- sizing
    // on the input count alone left the buffer undersized for low sample-rate sources.
    const int64_t kOutFramesPerCall = static_cast<int64_t>(kFrames) * out_rate / source_info.sample_rate + 1;
    vector<unsigned char> mp3(static_cast<size_t>(1.25 * kOutFramesPerCall + 7200));
    int64_t frames_read;
    while ((frames_read = in.read_float(pcm.data(), kFrames)) > 0) {
        if (cancel.load()) return "cancelled";
        int written = source_info.channels == 2
            ? lame_encode_buffer_interleaved_ieee_float(lame, pcm.data(), static_cast<int>(frames_read), mp3.data(), static_cast<int>(mp3.size()))
            : lame_encode_buffer_ieee_float(lame, pcm.data(), pcm.data(), static_cast<int>(frames_read), mp3.data(), static_cast<int>(mp3.size()));
        if (written < 0) return "lame encode error " + to_string(written);
        file.write(reinterpret_cast<const char*>(mp3.data()), written);
        if (!file) return "write failed: " + path_to_utf8(out);
    }
    int written = lame_encode_flush(lame, mp3.data(), static_cast<int>(mp3.size()));
    if (written < 0) return "lame flush error";
    file.write(reinterpret_cast<const char*>(mp3.data()), written);

    // Xing/Info frame: LAME reserved a frame at the start of the stream; fill it in now.
    size_t tag_size = lame_get_lametag_frame(lame, nullptr, 0);
    if (tag_size > 0) {
        vector<unsigned char> tag(tag_size);
        lame_get_lametag_frame(lame, tag.data(), tag.size());
        size_t id3_size = tags.empty() ? 0 : lame_get_id3v2_tag(lame, nullptr, 0);
        file.seekp(static_cast<streamoff>(id3_size));
        file.write(reinterpret_cast<const char*>(tag.data()), static_cast<streamsize>(tag.size()));
    }
    file.close();
    if (!file) return "write failed: " + path_to_utf8(out);
    return "";
}

}  // namespace beatdown
