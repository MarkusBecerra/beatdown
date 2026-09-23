#include <catch2/catch_test_macros.hpp>
#include "core/unicode.hpp"

using namespace beatdown;

TEST_CASE("utf8 <-> utf16 round-trips BMP and astral characters") {
    std::string s = "Måns Zelmerlöw – 東京 🎧";
    std::u16string w = utf8_to_utf16(s);
    REQUIRE(w == u"Måns Zelmerlöw – 東京 🎧");
    REQUIRE(utf16_to_utf8(w) == s);
}

TEST_CASE("utf16_to_utf8 honours a byte-order mark") {
    std::u16string le = std::u16string{char16_t(0xFEFF), u'a', u'b', u'c'};
    REQUIRE(utf16_to_utf8(le) == "abc");
    std::u16string swapped = { char16_t(0xFFFE), char16_t(0x6100), char16_t(0x6200) }; // "ab" byte-swapped
    REQUIRE(utf16_to_utf8(swapped) == "ab");
}

TEST_CASE("latin1_to_utf8 maps high bytes to two-byte sequences") {
    std::string in = "caf\xE9";
    REQUIRE(latin1_to_utf8(in) == "café");
}

TEST_CASE("path_to_utf8 round-trips a non-ASCII name through path_from_utf8") {
    std::string name = "Straße.wav";
    std::filesystem::path p = path_from_utf8(name);
    REQUIRE(path_to_utf8(p) == name);
}

TEST_CASE("path_from_utf8 preserves a non-ASCII filename's bytes when joined under a directory") {
    std::string filename = "Straße.wav";
    std::filesystem::path full = path_from_utf8("music") / path_from_utf8(filename);
    REQUIRE(path_to_utf8(full.filename()) == filename);
}

static const std::string kReplacement = "\xEF\xBF\xBD";   // U+FFFD in UTF-8

TEST_CASE("sanitize_utf8 returns valid UTF-8 unchanged") {
    for (std::string s : {"", "plain ASCII", "Måns Zelmerlöw – 東京 🎧",
                          "\xED\x9F\xBF", "\xEE\x80\x80", "\xF4\x8F\xBF\xBF", "\xEF\xBF\xBD"})   // U+D7FF, U+E000, U+10FFFF, U+FFFD
        REQUIRE(sanitize_utf8(s) == s);
}

TEST_CASE("sanitize_utf8 re-decodes text that isn't UTF-8 as Windows-1252") {
    REQUIRE(sanitize_utf8("Beyonc\xE9 Mix") == "Beyoncé Mix");
    REQUIRE(sanitize_utf8("\x80") == "€");
    REQUIRE(sanitize_utf8("\x81") == kReplacement);            // one of cp1252's five undefined bytes
    REQUIRE(sanitize_utf8("\x8D\x8F\x90\x9D") == kReplacement + kReplacement + kReplacement + kReplacement);
    REQUIRE(sanitize_utf8("\x93quoted\x94 \x96 caf\xE9") == "“quoted” – café");
}

TEST_CASE("sanitize_utf8 treats every malformed sequence as invalid (overlong / surrogate / beyond U+10FFFF / bad continuation / truncated)") {
    REQUIRE(sanitize_utf8("\xC0\xAF") == "\xC3\x80\xC2\xAF");                        // overlong "/" -> "À¯"
    REQUIRE(sanitize_utf8("\xED\xA0\x80") == "\xC3\xAD\xC2\xA0\xE2\x82\xAC");        // lone surrogate U+D800 -> "í", NBSP, "€"
    REQUIRE(sanitize_utf8("\xF4\x90\x80\x80") == "\xC3\xB4" + kReplacement + "€€");  // U+110000 -> "ô", U+FFFD, "€€"
    REQUIRE(sanitize_utf8("\xE2\x28\xA1") == "\xC3\xA2(\xC2\xA1");                   // bad continuation -> "â(¡"
    REQUIRE(sanitize_utf8("ab\xE2\x82") == "ab\xC3\xA2\xE2\x80\x9A");                // truncated "€" -> "abâ‚"
}

TEST_CASE("utf8_to_utf16 replaces an invalid byte with U+FFFD and keeps the bytes after it") {
    const char16_t r = char16_t(0xFFFD);
    REQUIRE(utf8_to_utf16("a\xE9" "b") == std::u16string{u'a', r, u'b'});
    REQUIRE(utf8_to_utf16("Beyonc\xE9 Mix") == u"Beyonc" + std::u16string(1, r) + u" Mix");
    REQUIRE(utf8_to_utf16("\xE2\x28\xA1") == std::u16string{r, u'(', r});
    REQUIRE(utf8_to_utf16("\xC0\xAF") == std::u16string{r, r});
    REQUIRE(utf8_to_utf16("\xED\xA0\x80") == std::u16string{r, r, r});
}

TEST_CASE("utf16_to_utf8 turns an unpaired surrogate into U+FFFD") {
    REQUIRE(utf16_to_utf8(std::u16string{u'a', char16_t(0xD800), u'b'}) == "a" + kReplacement + "b");
    REQUIRE(utf16_to_utf8(std::u16string{u'a', char16_t(0xDC00)}) == "a" + kReplacement);
    REQUIRE(utf16_to_utf8(std::u16string{u'a', char16_t(0xD83C)}) == "a" + kReplacement);   // high surrogate at the end
}
