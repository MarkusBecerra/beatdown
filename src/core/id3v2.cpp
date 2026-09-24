#include "core/id3v2.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include "core/unicode.hpp"

namespace beatdown {

static uint32_t syncsafe(const unsigned char* p) {
    return (uint32_t(p[0] & 0x7F) << 21) | (uint32_t(p[1] & 0x7F) << 14) | (uint32_t(p[2] & 0x7F) << 7) | uint32_t(p[3] & 0x7F);
}
static uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Decode an ID3 text payload (encoding byte already consumed). Strips trailing NULs.
static string decode_text(int enc, string_view data) {
    string out;
    if (enc == 0) out = latin1_to_utf8(data);
    else if (enc == 3) out = sanitize_utf8(data);   // declared UTF-8 is not always valid UTF-8
    else {
        u16string u;
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            uint16_t lo = static_cast<unsigned char>(data[i]), hi = static_cast<unsigned char>(data[i + 1]);
            u.push_back(enc == 2 ? char16_t((lo << 8) | hi) : char16_t(lo | (hi << 8)));  // BE (2) or BOM-led (1)
        }
        if (enc == 2) u.insert(u.begin(), char16_t(0xFEFF));
        out = utf16_to_utf8(u);
    }
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// Skip the terminator of a string in `data` at `pos` for the given encoding; returns index after it.
static size_t skip_terminated(int enc, string_view data, size_t pos) {
    if (enc == 1 || enc == 2) {
        for (; pos + 1 < data.size(); pos += 2) if (data[pos] == 0 && data[pos + 1] == 0) return pos + 2;
        return data.size();
    }
    for (; pos < data.size(); ++pos) if (data[pos] == 0) return pos + 1;
    return data.size();
}

bool parse_id3v2(string_view bytes, Tags& out, size_t* tag_size) {
    if (bytes.size() < 10 || bytes.substr(0, 3) != "ID3") return false;
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    int major = p[3];
    unsigned flags = p[5];
    if (major != 3 && major != 4) return false;
    size_t size = syncsafe(p + 6);
    size_t total = 10 + size + ((major == 4 && (flags & 0x10)) ? 10 : 0);
    if (bytes.size() < total) return false;
    if (tag_size) *tag_size = total;
    if (flags & 0x80) return true;  // unsynchronised: rare, not produced by any DAW we care about; tag counts as present but unread

    size_t pos = 10, end = 10 + size;
    if (flags & 0x40) {  // extended header
        if (pos + 4 > end) return true;
        size_t ext = major == 4 ? syncsafe(p + pos) : be32(p + pos) + 4;
        pos += ext;
    }
    while (pos + 10 <= end) {
        if (p[pos] == 0) break;  // padding
        string id(bytes.substr(pos, 4));
        size_t fsize = major == 4 ? syncsafe(p + pos + 4) : be32(p + pos + 4);
        pos += 10;
        if (fsize == 0 || pos + fsize > end) break;
        string_view data = bytes.substr(pos, fsize);
        pos += fsize;
        int enc = static_cast<unsigned char>(data[0]);
        if (enc > 3) continue;
        if (id[0] == 'T' && id != "TXXX") {
            string text = decode_text(enc, data.substr(1));
            if (text.empty()) continue;
            if (id == "TIT2") out.title = text;
            else if (id == "TPE1") out.artist = text;
            else if (id == "TALB") out.album = text;
            else if (id == "TYER" || id == "TDRC") out.date = text;
            else if (id == "TRCK") out.track = text;
            else if (id == "TCON") out.genre = text;
        } else if (id == "COMM" && data.size() > 4) {
            size_t after_desc = skip_terminated(enc, data, 4);
            string text = decode_text(enc, data.substr(after_desc));
            if (!text.empty() && !out.comment) out.comment = text;
        }
    }
    return true;
}

Tags read_wav_id3_chunk(const fs::path& wav) {
    Tags t;
    ifstream in(wav, ios::binary);
    char hdr[12];
    if (!in.read(hdr, 12) || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return t;
    while (in) {
        unsigned char ch[8];
        if (!in.read(reinterpret_cast<char*>(ch), 8)) break;
        uint32_t csize = uint32_t(ch[4]) | (uint32_t(ch[5]) << 8) | (uint32_t(ch[6]) << 16) | (uint32_t(ch[7]) << 24);
        if (memcmp(ch, "id3 ", 4) == 0 || memcmp(ch, "ID3 ", 4) == 0) {
            if (csize > (64u << 20)) return t;  // absurd; refuse to slurp
            string data(csize, '\0');
            if (!in.read(data.data(), csize)) return t;
            parse_id3v2(data, t);
            return t;
        }
        in.seekg(csize + (csize & 1), ios::cur);
    }
    return t;
}

// ---- test-only builder ----
static void frame(string& out, const char* id, int enc, const string& utf8, bool comment = false) {
    string payload(1, static_cast<char>(enc));
    if (comment) { payload += "eng"; payload += (enc == 1 || enc == 2) ? string("\0\0", 2) : string("\0", 1); }
    if (enc == 0) { for (char16_t c : utf8_to_utf16(utf8)) payload.push_back(static_cast<char>(c < 256 ? c : '?')); }
    else if (enc == 3) payload += utf8;
    else {
        u16string u;
        u.push_back(char16_t(0xFEFF));
        u += utf8_to_utf16(utf8);
        for (char16_t c : u) { payload.push_back(static_cast<char>(c & 0xFF)); payload.push_back(static_cast<char>(c >> 8)); }
    }
    uint32_t n = static_cast<uint32_t>(payload.size());
    out += id;
    out.push_back(static_cast<char>(n >> 24)); out.push_back(static_cast<char>(n >> 16)); out.push_back(static_cast<char>(n >> 8)); out.push_back(static_cast<char>(n));
    out += string("\0\0", 2);
    out += payload;
}

string build_id3v2_for_test(const Tags& t, int enc) {
    string body;
    if (t.title) frame(body, "TIT2", enc, *t.title);
    if (t.artist) frame(body, "TPE1", enc, *t.artist);
    if (t.album) frame(body, "TALB", enc, *t.album);
    if (t.date) frame(body, "TYER", enc, *t.date);
    if (t.track) frame(body, "TRCK", enc, *t.track);
    if (t.genre) frame(body, "TCON", enc, *t.genre);
    if (t.comment) frame(body, "COMM", enc, *t.comment, true);
    uint32_t n = static_cast<uint32_t>(body.size());
    string out = "ID3";
    out.push_back(3); out.push_back(0); out.push_back(0);
    out.push_back(static_cast<char>((n >> 21) & 0x7F)); out.push_back(static_cast<char>((n >> 14) & 0x7F));
    out.push_back(static_cast<char>((n >> 7) & 0x7F)); out.push_back(static_cast<char>(n & 0x7F));
    return out + body;
}

}  // namespace beatdown
