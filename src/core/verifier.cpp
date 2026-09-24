#include "core/verifier.hpp"
#include <cmath>
#include "core/mp3_encoder.hpp"
#include "core/mp3_parse.hpp"
#include "core/std_names.hpp"

namespace beatdown {

string verify_mp3(const fs::path& out, const EncodeSettings& s, const AudioInfo& src) {
    error_code ec;
    if (!fs::exists(out, ec)) return "output missing";
    Mp3Info info;
    string err;
    if (!parse_mp3(out, info, err)) return "output is not a readable MP3: " + err;
    // Task 18 fix round 2: a fixed byte-size floor (previously 1024) rejected legitimate short
    // and/or low-bitrate outputs (a 0.05 s 128 kbps file is well under that). "At least one real
    // audio frame beyond the Info/Xing frame" is the actual property that matters and scales
    // correctly with duration and bitrate instead of guessing a byte count.
    if (info.audio_frames < 1) return "output has no audio frames";
    if (info.trailing_bytes > 128)
        return "output has " + to_string(info.trailing_bytes) + " trailing bytes after the last frame";
    int expected_rate = mp3_output_rate(src.sample_rate);
    if (info.sample_rate != expected_rate)
        return "sample rate " + to_string(info.sample_rate) + " Hz, expected " + to_string(expected_rate);
    if (s.vbr) {
        if (!info.has_xing) return "VBR stream lacks a Xing header";
    } else if (!info.cbr(s.bitrate)) {
        string seen;
        for (int b : info.bitrates) seen += (seen.empty() ? "" : "/") + to_string(b);
        return "bitrate " + seen + " kbps, expected CBR " + to_string(s.bitrate);
    }
    double want = src.seconds(), got = info.duration_seconds();
    if (fabs(want - got) > 1.0)
        return "duration " + to_string(got) + " s, expected " + to_string(want) + " s";
    return "";
}

string verify_flac(const fs::path& out, const AudioInfo& src) {
    error_code ec;
    if (!fs::exists(out, ec)) return "output missing";
    string err;
    auto d = Decoder::open(out, err);
    if (!d) return "output is not a readable FLAC: " + err;
    const AudioInfo& o = d->info();
    if (o.channels != src.channels)
        return "channels " + to_string(o.channels) + ", expected " + to_string(src.channels);
    if (o.sample_rate != src.sample_rate)
        return "sample rate " + to_string(o.sample_rate) + ", expected " + to_string(src.sample_rate);
    if (o.frames != src.frames)
        return "frames " + to_string(o.frames) + ", expected " + to_string(src.frames);
    return "";
}

string verify_output(const fs::path& out, const EncodeSettings& s, const AudioInfo& src) {
    return s.format == Format::Flac ? verify_flac(out, src) : verify_mp3(out, s, src);
}

}  // namespace beatdown
