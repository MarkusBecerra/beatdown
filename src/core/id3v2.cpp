#include "core/id3v2.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include "core/unicode.hpp"

namespace beatdown {

static uint32_t read_syncsafe(const unsigned char* bytes) {
    return (uint32_t(bytes[0] & 0x7F) << 21) | (uint32_t(bytes[1] & 0x7F) << 14) | (uint32_t(bytes[2] & 0x7F) << 7) | uint32_t(bytes[3] & 0x7F);
}
static uint32_t read_be32(const unsigned char* bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

// Decode an ID3 text payload (encoding byte already consumed). Strips trailing NULs.
static string decode_text(int encoding, string_view data) {
    string out;
    if (encoding == 0) out = latin1_to_utf8(data);
    else if (encoding == 3) out = sanitize_utf8(data);   // declared UTF-8 is not always valid UTF-8
    else {
        u16string units;
        for (size_t index = 0; index + 1 < data.size(); index += 2) {
            uint16_t low_byte = static_cast<unsigned char>(data[index]), high_byte = static_cast<unsigned char>(data[index + 1]);
            units.push_back(encoding == 2 ? char16_t((low_byte << 8) | high_byte) : char16_t(low_byte | (high_byte << 8)));  // BE (2) or BOM-led (1)
        }
        if (encoding == 2) units.insert(units.begin(), char16_t(0xFEFF));
        out = utf16_to_utf8(units);
    }
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// Skip the terminator of a string in `data` at `pos` for the given encoding; returns index after it.
static size_t skip_terminated(int encoding, string_view data, size_t pos) {
    if (encoding == 1 || encoding == 2) {
        for (; pos + 1 < data.size(); pos += 2) if (data[pos] == 0 && data[pos + 1] == 0) return pos + 2;
        return data.size();
    }
    for (; pos < data.size(); ++pos) if (data[pos] == 0) return pos + 1;
    return data.size();
}

bool parse_id3v2(string_view bytes, Tags& out, size_t* tag_size) {
    if (bytes.size() < 10 || bytes.substr(0, 3) != "ID3") return false;
    const auto* raw_bytes = reinterpret_cast<const unsigned char*>(bytes.data());
    int major = raw_bytes[3];
    unsigned flags = raw_bytes[5];
    if (major != 3 && major != 4) return false;
    size_t size = read_syncsafe(raw_bytes + 6);
    size_t total = 10 + size + ((major == 4 && (flags & 0x10)) ? 10 : 0);
    if (bytes.size() < total) return false;
    if (tag_size) *tag_size = total;
    if (flags & 0x80) return true;  // unsynchronised: rare, not produced by any DAW we care about; tag counts as present but unread

    size_t pos = 10, end = 10 + size;
    if (flags & 0x40) {  // extended header
        if (pos + 4 > end) return true;
        size_t ext_header_size = major == 4 ? read_syncsafe(raw_bytes + pos) : read_be32(raw_bytes + pos) + 4;
        pos += ext_header_size;
    }
    while (pos + 10 <= end) {
        if (raw_bytes[pos] == 0) break;  // padding
        string id(bytes.substr(pos, 4));
        size_t frame_size = major == 4 ? read_syncsafe(raw_bytes + pos + 4) : read_be32(raw_bytes + pos + 4);
        pos += 10;
        if (frame_size == 0 || pos + frame_size > end) break;
        string_view data = bytes.substr(pos, frame_size);
        pos += frame_size;
        int encoding = static_cast<unsigned char>(data[0]);
        if (encoding > 3) continue;
        if (id[0] == 'T' && id != "TXXX") {
            string text = decode_text(encoding, data.substr(1));
            if (text.empty()) continue;
            if (id == "TIT2") out.title = text;
            else if (id == "TPE1") out.artist = text;
            else if (id == "TALB") out.album = text;
            else if (id == "TYER" || id == "TDRC") out.date = text;
            else if (id == "TRCK") out.track = text;
            else if (id == "TCON") out.genre = text;
        } else if (id == "COMM" && data.size() > 4) {
            size_t after_desc = skip_terminated(encoding, data, 4);
            string text = decode_text(encoding, data.substr(after_desc));
            if (!text.empty() && !out.comment) out.comment = text;
        }
    }
    return true;
}

Tags read_wav_id3_chunk(const fs::path& wav) {
    Tags tags;
    ifstream in(wav, ios::binary);
    char header[12];
    if (!in.read(header, 12) || memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) return tags;
    while (in) {
        unsigned char chunk_header[8];
        if (!in.read(reinterpret_cast<char*>(chunk_header), 8)) break;
        uint32_t chunk_size = uint32_t(chunk_header[4]) | (uint32_t(chunk_header[5]) << 8) | (uint32_t(chunk_header[6]) << 16) | (uint32_t(chunk_header[7]) << 24);
        if (memcmp(chunk_header, "id3 ", 4) == 0 || memcmp(chunk_header, "ID3 ", 4) == 0) {
            if (chunk_size > (64u << 20)) return tags;  // absurd; refuse to slurp
            string data(chunk_size, '\0');
            if (!in.read(data.data(), chunk_size)) return tags;
            parse_id3v2(data, tags);
            return tags;
        }
        in.seekg(chunk_size + (chunk_size & 1), ios::cur);
    }
    return tags;
}

// ---- test-only builder ----
static void append_frame(string& out, const char* id, int encoding, const string& utf8, bool comment = false) {
    string payload(1, static_cast<char>(encoding));
    if (comment) { payload += "eng"; payload += (encoding == 1 || encoding == 2) ? string("\0\0", 2) : string("\0", 1); }
    if (encoding == 0) { for (char16_t unit : utf8_to_utf16(utf8)) payload.push_back(static_cast<char>(unit < 256 ? unit : '?')); }
    else if (encoding == 3) payload += utf8;
    else {
        u16string units;
        units.push_back(char16_t(0xFEFF));
        units += utf8_to_utf16(utf8);
        for (char16_t unit : units) { payload.push_back(static_cast<char>(unit & 0xFF)); payload.push_back(static_cast<char>(unit >> 8)); }
    }
    uint32_t payload_size = static_cast<uint32_t>(payload.size());
    out += id;
    out.push_back(static_cast<char>(payload_size >> 24)); out.push_back(static_cast<char>(payload_size >> 16)); out.push_back(static_cast<char>(payload_size >> 8)); out.push_back(static_cast<char>(payload_size));
    out += string("\0\0", 2);
    out += payload;
}

string build_id3v2_for_test(const Tags& tags, int encoding) {
    string body;
    if (tags.title) append_frame(body, "TIT2", encoding, *tags.title);
    if (tags.artist) append_frame(body, "TPE1", encoding, *tags.artist);
    if (tags.album) append_frame(body, "TALB", encoding, *tags.album);
    if (tags.date) append_frame(body, "TYER", encoding, *tags.date);
    if (tags.track) append_frame(body, "TRCK", encoding, *tags.track);
    if (tags.genre) append_frame(body, "TCON", encoding, *tags.genre);
    if (tags.comment) append_frame(body, "COMM", encoding, *tags.comment, true);
    uint32_t body_size = static_cast<uint32_t>(body.size());
    string out = "ID3";
    out.push_back(3); out.push_back(0); out.push_back(0);
    out.push_back(static_cast<char>((body_size >> 21) & 0x7F)); out.push_back(static_cast<char>((body_size >> 14) & 0x7F));
    out.push_back(static_cast<char>((body_size >> 7) & 0x7F)); out.push_back(static_cast<char>(body_size & 0x7F));
    return out + body;
}

}  // namespace beatdown
