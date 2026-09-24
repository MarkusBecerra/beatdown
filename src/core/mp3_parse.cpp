#include "core/mp3_parse.hpp"
#include <cstring>
#include <fstream>
#include "core/id3v2.hpp"

namespace beatdown {

namespace {
const int kBitrateV1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1};
const int kBitrateV2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1};
const int kRateV1[4] = {44100, 48000, 32000, -1};
const int kRateV2[4] = {22050, 24000, 16000, -1};
const int kRateV25[4] = {11025, 12000, 8000, -1};

struct Frame { int length; int bitrate; int sample_rate; int spf; bool mono; bool crc; int side_info; };

// Layer III only. Returns false if the 4 bytes are not a valid frame header.
bool parse_header(const unsigned char* h, Frame& f) {
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    int version = (h[1] >> 3) & 3;   // 0=2.5 1=reserved 2=2 3=1
    int layer = (h[1] >> 1) & 3;     // 1 = Layer III
    if (version == 1 || layer != 1) return false;
    int bi = h[2] >> 4, si = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (bi == 0 || bi == 15 || si == 3) return false;
    bool v1 = version == 3;
    f.crc = (h[1] & 1) == 0;
    f.bitrate = v1 ? kBitrateV1[bi] : kBitrateV2[bi];
    f.sample_rate = v1 ? kRateV1[si] : (version == 2 ? kRateV2[si] : kRateV25[si]);
    f.spf = v1 ? 1152 : 576;
    f.mono = (h[3] >> 6) == 3;
    f.side_info = v1 ? (f.mono ? 17 : 32) : (f.mono ? 9 : 17);
    f.length = (v1 ? 144 : 72) * f.bitrate * 1000 / f.sample_rate + pad;
    return true;
}
}  // namespace

bool parse_mp3(const fs::path& file, Mp3Info& out, string& error) {
    out = Mp3Info{};
    ifstream in(file, ios::binary);
    string data((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    if (data.empty()) { error = "empty file"; return false; }
    size_t pos = 0;
    if (parse_id3v2(data, out.tags, &out.id3v2_size)) pos = out.id3v2_size;
    const auto* p = reinterpret_cast<const unsigned char*>(data.data());
    bool first = true;
    while (pos + 4 <= data.size()) {
        Frame f;
        if (!parse_header(p + pos, f) || pos + f.length > data.size()) break;
        if (first) {
            out.sample_rate = f.sample_rate;
            out.channels = f.mono ? 1 : 2;
            out.samples_per_frame = f.spf;
            size_t tag_at = pos + 4 + (f.crc ? 2 : 0) + f.side_info;
            if (tag_at + 4 <= data.size() && (memcmp(p + tag_at, "Xing", 4) == 0 || memcmp(p + tag_at, "Info", 4) == 0)) {
                out.has_xing = true;
                pos += f.length;
                first = false;
                continue;
            }
            first = false;
        }
        ++out.audio_frames;
        out.bitrates.insert(f.bitrate);
        pos += f.length;
    }
    out.trailing_bytes = data.size() - pos;
    if (out.audio_frames == 0 && !out.has_xing) { error = "no MP3 frames found"; return false; }
    return true;
}

string build_mp3_frame_for_test(int kbps, int sample_rate, bool padding, bool mono, const char* xing_tag, bool crc) {
    int bi = 0; for (int i = 1; i < 15; ++i) if (kBitrateV1[i] == kbps) bi = i;
    int si = 0; for (int i = 0; i < 3; ++i) if (kRateV1[i] == sample_rate) si = i;
    unsigned char h[4] = {0xFF, static_cast<unsigned char>(crc ? 0xFA : 0xFB), static_cast<unsigned char>((bi << 4) | (si << 2) | (padding ? 2 : 0)), static_cast<unsigned char>(mono ? 0xC0 : 0x00)};
    int length = 144 * kbps * 1000 / sample_rate + (padding ? 1 : 0);
    string frame(reinterpret_cast<const char*>(h), 4);
    if (crc) frame.resize(frame.size() + 2, '\0');  // Add CRC bytes
    frame.resize(length, '\0');
    if (xing_tag) memcpy(frame.data() + 4 + (crc ? 2 : 0) + (mono ? 17 : 32), xing_tag, 4);
    return frame;
}

}  // namespace beatdown
