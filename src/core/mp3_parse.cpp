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

struct Frame { int length; int bitrate; int sample_rate; int samples_per_frame; bool mono; bool crc; int side_info; };

// Layer III only. Returns false if the 4 bytes are not a valid frame header.
bool parse_header(const unsigned char* header, Frame& frame) {
    if (header[0] != 0xFF || (header[1] & 0xE0) != 0xE0) return false;
    int version = (header[1] >> 3) & 3;   // 0=2.5 1=reserved 2=2 3=1
    int layer = (header[1] >> 1) & 3;     // 1 = Layer III
    if (version == 1 || layer != 1) return false;
    int bitrate_index = header[2] >> 4, rate_index = (header[2] >> 2) & 3, padding_bit = (header[2] >> 1) & 1;
    if (bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) return false;
    bool is_v1 = version == 3;
    frame.crc = (header[1] & 1) == 0;
    frame.bitrate = is_v1 ? kBitrateV1[bitrate_index] : kBitrateV2[bitrate_index];
    frame.sample_rate = is_v1 ? kRateV1[rate_index] : (version == 2 ? kRateV2[rate_index] : kRateV25[rate_index]);
    frame.samples_per_frame = is_v1 ? 1152 : 576;
    frame.mono = (header[3] >> 6) == 3;
    frame.side_info = is_v1 ? (frame.mono ? 17 : 32) : (frame.mono ? 9 : 17);
    frame.length = (is_v1 ? 144 : 72) * frame.bitrate * 1000 / frame.sample_rate + padding_bit;
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
    const auto* raw_data = reinterpret_cast<const unsigned char*>(data.data());
    bool first = true;
    while (pos + 4 <= data.size()) {
        Frame frame;
        if (!parse_header(raw_data + pos, frame) || pos + frame.length > data.size()) break;
        if (first) {
            out.sample_rate = frame.sample_rate;
            out.channels = frame.mono ? 1 : 2;
            out.samples_per_frame = frame.samples_per_frame;
            size_t tag_at = pos + 4 + (frame.crc ? 2 : 0) + frame.side_info;
            if (tag_at + 4 <= data.size() && (memcmp(raw_data + tag_at, "Xing", 4) == 0 || memcmp(raw_data + tag_at, "Info", 4) == 0)) {
                out.has_xing = true;
                pos += frame.length;
                first = false;
                continue;
            }
            first = false;
        }
        ++out.audio_frames;
        out.bitrates.insert(frame.bitrate);
        pos += frame.length;
    }
    out.trailing_bytes = data.size() - pos;
    if (out.audio_frames == 0 && !out.has_xing) { error = "no MP3 frames found"; return false; }
    return true;
}

string build_mp3_frame_for_test(int kbps, int sample_rate, bool padding, bool mono, const char* xing_tag, bool crc) {
    int bitrate_index = 0; for (int index = 1; index < 15; ++index) if (kBitrateV1[index] == kbps) bitrate_index = index;
    int rate_index = 0; for (int index = 0; index < 3; ++index) if (kRateV1[index] == sample_rate) rate_index = index;
    unsigned char header[4] = {0xFF, static_cast<unsigned char>(crc ? 0xFA : 0xFB), static_cast<unsigned char>((bitrate_index << 4) | (rate_index << 2) | (padding ? 2 : 0)), static_cast<unsigned char>(mono ? 0xC0 : 0x00)};
    int length = 144 * kbps * 1000 / sample_rate + (padding ? 1 : 0);
    string frame(reinterpret_cast<const char*>(header), 4);
    if (crc) frame.resize(frame.size() + 2, '\0');  // Add CRC bytes
    frame.resize(length, '\0');
    if (xing_tag) memcpy(frame.data() + 4 + (crc ? 2 : 0) + (mono ? 17 : 32), xing_tag, 4);
    return frame;
}

}  // namespace beatdown
