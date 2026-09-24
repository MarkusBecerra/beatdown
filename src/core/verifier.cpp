#include "core/verifier.hpp"
#include <cmath>
#include "core/mp3_encoder.hpp"
#include "core/mp3_parse.hpp"
#include "core/std_names.hpp"

namespace beatdown {

string verify_mp3(const fs::path& out, const EncodeSettings& settings, const AudioInfo& source_info) {
    error_code fs_error;
    if (!fs::exists(out, fs_error)) return "output missing";
    Mp3Info info;
    string error_message;
    if (!parse_mp3(out, info, error_message)) return "output is not a readable MP3: " + error_message;
    // Task 18 fix round 2: a fixed byte-size floor (previously 1024) rejected legitimate short
    // and/or low-bitrate outputs (a 0.05 s 128 kbps file is well under that). "At least one real
    // audio frame beyond the Info/Xing frame" is the actual property that matters and scales
    // correctly with duration and bitrate instead of guessing a byte count.
    if (info.audio_frames < 1) return "output has no audio frames";
    if (info.trailing_bytes > 128)
        return "output has " + to_string(info.trailing_bytes) + " trailing bytes after the last frame";
    int expected_rate = mp3_output_rate(source_info.sample_rate);
    if (info.sample_rate != expected_rate)
        return "sample rate " + to_string(info.sample_rate) + " Hz, expected " + to_string(expected_rate);
    if (settings.vbr) {
        if (!info.has_xing) return "VBR stream lacks a Xing header";
    } else if (!info.cbr(settings.bitrate)) {
        string seen;
        for (int bitrate : info.bitrates) seen += (seen.empty() ? "" : "/") + to_string(bitrate);
        return "bitrate " + seen + " kbps, expected CBR " + to_string(settings.bitrate);
    }
    double want = source_info.seconds(), got = info.duration_seconds();
    if (fabs(want - got) > 1.0)
        return "duration " + to_string(got) + " s, expected " + to_string(want) + " s";
    return "";
}

string verify_flac(const fs::path& out, const AudioInfo& source_info) {
    error_code fs_error;
    if (!fs::exists(out, fs_error)) return "output missing";
    string error_message;
    auto decoder = Decoder::open(out, error_message);
    if (!decoder) return "output is not a readable FLAC: " + error_message;
    const AudioInfo& output_info = decoder->info();
    if (output_info.channels != source_info.channels)
        return "channels " + to_string(output_info.channels) + ", expected " + to_string(source_info.channels);
    if (output_info.sample_rate != source_info.sample_rate)
        return "sample rate " + to_string(output_info.sample_rate) + ", expected " + to_string(source_info.sample_rate);
    if (output_info.frames != source_info.frames)
        return "frames " + to_string(output_info.frames) + ", expected " + to_string(source_info.frames);
    return "";
}

string verify_output(const fs::path& out, const EncodeSettings& settings, const AudioInfo& source_info) {
    return settings.format == Format::Flac ? verify_flac(out, source_info) : verify_mp3(out, settings, source_info);
}

}  // namespace beatdown
