#include "core/unicode.hpp"
#include <cstdint>

namespace beatdown {

namespace {

constexpr uint32_t kReplacement = 0xFFFD;

// Decodes the UTF-8 sequence starting at s[i] into `cp` and returns its length, or returns 0
// if the bytes there aren't valid UTF-8: a stray continuation byte or an impossible lead byte,
// a missing or bad continuation byte (including a sequence cut off by the end of `s`), an
// overlong form, a UTF-16 surrogate (U+D800–U+DFFF) or a code point beyond U+10FFFF.
size_t decode_utf8(string_view s, size_t i, uint32_t& cp) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len;
    uint32_t min;
    if (c < 0x80) { cp = c; return 1; }
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; min = 0x10000; }
    else return 0;   // 0x80–0xBF continuation, 0xC0/0xC1 always overlong, 0xF5–0xFF beyond U+10FFFF
    if (s.size() - i < len) return 0;
    for (size_t k = 1; k < len; ++k) {
        unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return len;
}

void append_utf8(string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
}

// Windows-1252's 0x80–0x9F row; 0 marks its five undefined bytes (0x81 0x8D 0x8F 0x90 0x9D).
// Every other byte maps to the code point of the same value (ASCII, then Latin-1 from 0xA0).
constexpr uint16_t kCp1252Row8[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80–87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,       // 88–8F
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90–97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,  // 98–9F
};

string cp1252_to_utf8(string_view s) {
    string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        uint32_t cp = c;
        if (c >= 0x80 && c <= 0x9F) cp = kCp1252Row8[c - 0x80] ? kCp1252Row8[c - 0x80] : kReplacement;
        append_utf8(out, cp);
    }
    return out;
}

}  // namespace

u16string utf8_to_utf16(string_view s) {
    u16string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp;
        size_t len = decode_utf8(s, i, cp);
        if (len == 0) { cp = kReplacement; len = 1; }   // one bad byte, one U+FFFD; resync on the next byte
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

string utf16_to_utf8(u16string_view s) {
    bool swap = false;
    if (!s.empty() && s[0] == 0xFEFF) s.remove_prefix(1);
    else if (!s.empty() && s[0] == 0xFFFE) { swap = true; s.remove_prefix(1); }
    auto unit = [&](size_t i) -> uint32_t {
        uint32_t u = s[i];
        return swap ? ((u >> 8) | ((u & 0xFF) << 8)) : u;
    };
    string out;
    out.reserve(s.size() * 3);
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t u = unit(i);
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < s.size()) {
            uint32_t lo = unit(i + 1);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                append_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                ++i;
                continue;
            }
        }
        if (u >= 0xD800 && u <= 0xDFFF) u = kReplacement;   // unpaired surrogate
        append_utf8(out, u);
    }
    return out;
}

string latin1_to_utf8(string_view s) {
    string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) append_utf8(out, c);
    return out;
}

string sanitize_utf8(string_view s) {
    string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp;
        size_t len = decode_utf8(s, i, cp);
        if (len == 0) return cp1252_to_utf8(s);
        // U+FFFE/U+FFFF are valid code points (decode_utf8 accepts them) but libFLAC's
        // utf8len_ rejects exactly these two, and libsndfile 1.2.2 ignores that rejection and
        // appends an uninitialised vorbis-comment entry instead of failing, which crashes.
        // Replace just these two code points; everything else libFLAC rejects (overlongs,
        // surrogates, structurally invalid bytes) is already invalid UTF-8 here and takes the
        // cp1252 path above, never reaching libFLAC unsanitized.
        if (cp == 0xFFFE || cp == 0xFFFF) append_utf8(out, kReplacement);
        else out.append(s.data() + i, len);
        i += len;
    }
    return out;
}

string path_to_utf8(const fs::path& p) {
    auto u8 = p.u8string();
    return string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path path_from_utf8(string_view s) {
    u8string u8(reinterpret_cast<const char8_t*>(s.data()), s.size());
    return fs::path(u8);
}

}  // namespace beatdown
