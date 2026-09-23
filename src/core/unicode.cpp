#include "core/unicode.hpp"
#include <cstdint>

namespace beatdown {

std::u16string utf8_to_utf16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { cp = 0xFFFD; len = 1; }
        if (i + len > s.size()) { cp = 0xFFFD; len = 1; }
        for (size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
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

static void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
}

std::string utf16_to_utf8(std::u16string_view s) {
    bool swap = false;
    if (!s.empty() && s[0] == 0xFEFF) s.remove_prefix(1);
    else if (!s.empty() && s[0] == 0xFFFE) { swap = true; s.remove_prefix(1); }
    auto unit = [&](size_t i) -> uint32_t {
        uint32_t u = s[i];
        return swap ? ((u >> 8) | ((u & 0xFF) << 8)) : u;
    };
    std::string out;
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
        append_utf8(out, u);
    }
    return out;
}

std::string latin1_to_utf8(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) append_utf8(out, c);
    return out;
}

std::string path_to_utf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path path_from_utf8(std::string_view s) {
    std::u8string u8(reinterpret_cast<const char8_t*>(s.data()), s.size());
    return std::filesystem::path(u8);
}

}  // namespace beatdown
