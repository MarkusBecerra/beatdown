#include "core/verifier.hpp"
#include <cmath>
#include "core/mp3_encoder.hpp"
#include "core/mp3_parse.hpp"

namespace fs = std::filesystem;

namespace beatdown {

std::string verify_mp3(const fs::path& out, const EncodeSettings& s, const AudioInfo& src) {
    std::error_code ec;
    if (!fs::exists(out, ec)) return "output missing";
    if (fs::file_size(out, ec) < 1024) return "output is implausibly small";
    Mp3Info info;
    std::string err;
    if (!parse_mp3(out, info, err)) return "output is not a readable MP3: " + err;
    if (info.trailing_bytes > 128)
        return "output has " + std::to_string(info.trailing_bytes) + " trailing bytes after the last frame";
    int expected_rate = mp3_output_rate(src.sample_rate);
    if (info.sample_rate != expected_rate)
        return "sample rate " + std::to_string(info.sample_rate) + " Hz, expected " + std::to_string(expected_rate);
    if (s.vbr) {
        if (!info.has_xing) return "VBR stream lacks a Xing header";
    } else if (!info.cbr(s.bitrate)) {
        std::string seen;
        for (int b : info.bitrates) seen += (seen.empty() ? "" : "/") + std::to_string(b);
        return "bitrate " + seen + " kbps, expected CBR " + std::to_string(s.bitrate);
    }
    double want = src.seconds(), got = info.duration_seconds();
    if (std::fabs(want - got) > 1.0)
        return "duration " + std::to_string(got) + " s, expected " + std::to_string(want) + " s";
    return "";
}

std::string verify_flac(const fs::path& out, const AudioInfo& src) {
    std::error_code ec;
    if (!fs::exists(out, ec)) return "output missing";
    std::string err;
    auto d = Decoder::open(out, err);
    if (!d) return "output is not a readable FLAC: " + err;
    const AudioInfo& o = d->info();
    if (o.channels != src.channels)
        return "channels " + std::to_string(o.channels) + ", expected " + std::to_string(src.channels);
    if (o.sample_rate != src.sample_rate)
        return "sample rate " + std::to_string(o.sample_rate) + ", expected " + std::to_string(src.sample_rate);
    if (o.frames != src.frames)
        return "frames " + std::to_string(o.frames) + ", expected " + std::to_string(src.frames);
    return "";
}

std::string verify_output(const fs::path& out, const EncodeSettings& s, const AudioInfo& src) {
    return s.format == Format::Flac ? verify_flac(out, src) : verify_mp3(out, s, src);
}

}  // namespace beatdown
