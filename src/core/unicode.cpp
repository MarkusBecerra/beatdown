#include "core/unicode.hpp"
#include <cstdint>

namespace beatdown {

namespace {

constexpr uint32_t kReplacement = 0xFFFD;

// Decodes the UTF-8 sequence starting at text[start] into `code_point` and returns its length, or
// returns 0 if the bytes there aren't valid UTF-8: a stray continuation byte or an impossible lead
// byte, a missing or bad continuation byte (including a sequence cut off by the end of `text`), an
// overlong form, a UTF-16 surrogate (U+D800–U+DFFF) or a code point beyond U+10FFFF.
size_t decode_utf8(string_view text, size_t start, uint32_t& code_point) {
    unsigned char lead_byte = static_cast<unsigned char>(text[start]);
    size_t length;
    uint32_t min_code_point;
    if (lead_byte < 0x80) { code_point = lead_byte; return 1; }
    if (lead_byte >= 0xC2 && lead_byte <= 0xDF) { length = 2; code_point = lead_byte & 0x1F; min_code_point = 0x80; }
    else if (lead_byte >= 0xE0 && lead_byte <= 0xEF) { length = 3; code_point = lead_byte & 0x0F; min_code_point = 0x800; }
    else if (lead_byte >= 0xF0 && lead_byte <= 0xF4) { length = 4; code_point = lead_byte & 0x07; min_code_point = 0x10000; }
    else return 0;   // 0x80–0xBF continuation, 0xC0/0xC1 always overlong, 0xF5–0xFF beyond U+10FFFF
    if (text.size() - start < length) return 0;
    for (size_t offset = 1; offset < length; ++offset) {
        unsigned char continuation_byte = static_cast<unsigned char>(text[start + offset]);
        if ((continuation_byte & 0xC0) != 0x80) return 0;
        code_point = (code_point << 6) | (continuation_byte & 0x3F);
    }
    if (code_point < min_code_point || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) return 0;
    return length;
}

void append_utf8(string& out, uint32_t code_point) {
    if (code_point < 0x80) out.push_back(static_cast<char>(code_point));
    else if (code_point < 0x800) { out.push_back(static_cast<char>(0xC0 | (code_point >> 6))); out.push_back(static_cast<char>(0x80 | (code_point & 0x3F))); }
    else if (code_point < 0x10000) { out.push_back(static_cast<char>(0xE0 | (code_point >> 12))); out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (code_point & 0x3F))); }
    else { out.push_back(static_cast<char>(0xF0 | (code_point >> 18))); out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (code_point & 0x3F))); }
}

// Windows-1252's 0x80–0x9F row; 0 marks its five undefined bytes (0x81 0x8D 0x8F 0x90 0x9D).
// Every other byte maps to the code point of the same value (ASCII, then Latin-1 from 0xA0).
constexpr uint16_t kCp1252Row8[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80–87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,       // 88–8F
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90–97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,  // 98–9F
};

string cp1252_to_utf8(string_view text) {
    string out;
    out.reserve(text.size() * 2);
    for (unsigned char byte : text) {
        uint32_t code_point = byte;
        if (byte >= 0x80 && byte <= 0x9F) code_point = kCp1252Row8[byte - 0x80] ? kCp1252Row8[byte - 0x80] : kReplacement;
        append_utf8(out, code_point);
    }
    return out;
}

}  // namespace

u16string utf8_to_utf16(string_view text) {
    u16string out;
    out.reserve(text.size());
    size_t index = 0;
    while (index < text.size()) {
        uint32_t code_point;
        size_t length = decode_utf8(text, index, code_point);
        if (length == 0) { code_point = kReplacement; length = 1; }   // one bad byte, one U+FFFD; resync on the next byte
        index += length;
        if (code_point >= 0x10000) {
            code_point -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (code_point >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (code_point & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(code_point));
        }
    }
    return out;
}

string utf16_to_utf8(u16string_view units) {
    bool swap = false;
    if (!units.empty() && units[0] == 0xFEFF) units.remove_prefix(1);
    else if (!units.empty() && units[0] == 0xFFFE) { swap = true; units.remove_prefix(1); }
    auto unit_at = [&](size_t index) -> uint32_t {
        uint32_t unit_value = units[index];
        return swap ? ((unit_value >> 8) | ((unit_value & 0xFF) << 8)) : unit_value;
    };
    string out;
    out.reserve(units.size() * 3);
    for (size_t index = 0; index < units.size(); ++index) {
        uint32_t unit_value = unit_at(index);
        if (unit_value >= 0xD800 && unit_value <= 0xDBFF && index + 1 < units.size()) {
            uint32_t low_surrogate = unit_at(index + 1);
            if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                append_utf8(out, 0x10000 + ((unit_value - 0xD800) << 10) + (low_surrogate - 0xDC00));
                ++index;
                continue;
            }
        }
        if (unit_value >= 0xD800 && unit_value <= 0xDFFF) unit_value = kReplacement;   // unpaired surrogate
        append_utf8(out, unit_value);
    }
    return out;
}

string latin1_to_utf8(string_view text) {
    string out;
    out.reserve(text.size() * 2);
    for (unsigned char byte : text) append_utf8(out, byte);
    return out;
}

string sanitize_utf8(string_view text) {
    string out;
    out.reserve(text.size());
    size_t index = 0;
    while (index < text.size()) {
        uint32_t code_point;
        size_t length = decode_utf8(text, index, code_point);
        if (length == 0) return cp1252_to_utf8(text);
        // U+FFFE/U+FFFF are valid code points (decode_utf8 accepts them) but libFLAC's
        // utf8len_ rejects exactly these two, and libsndfile 1.2.2 ignores that rejection and
        // appends an uninitialised vorbis-comment entry instead of failing, which crashes.
        // Replace just these two code points; everything else libFLAC rejects (overlongs,
        // surrogates, structurally invalid bytes) is already invalid UTF-8 here and takes the
        // cp1252 path above, never reaching libFLAC unsanitized.
        if (code_point == 0xFFFE || code_point == 0xFFFF) append_utf8(out, kReplacement);
        else out.append(text.data() + index, length);
        index += length;
    }
    return out;
}

string path_to_utf8(const fs::path& path) {
    auto code_units = path.u8string();
    return string(reinterpret_cast<const char*>(code_units.data()), code_units.size());
}

fs::path path_from_utf8(string_view text) {
    u8string code_units(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return fs::path(code_units);
}

}  // namespace beatdown
