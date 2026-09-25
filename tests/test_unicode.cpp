#include <catch2/catch_test_macros.hpp>
#include "core/unicode.hpp"

using namespace beatdown;

TEST_CASE("utf8 <-> utf16 round-trips BMP and astral characters") {
    string text = "Måns Zelmerlöw – 東京 🎧";
    u16string wide = utf8_to_utf16(text);
    REQUIRE(wide == u"Måns Zelmerlöw – 東京 🎧");
    REQUIRE(utf16_to_utf8(wide) == text);
}

TEST_CASE("utf16_to_utf8 honours a byte-order mark") {
    u16string le = u16string{char16_t(0xFEFF), u'a', u'b', u'c'};
    REQUIRE(utf16_to_utf8(le) == "abc");
    u16string swapped = { char16_t(0xFFFE), char16_t(0x6100), char16_t(0x6200) }; // "ab" byte-swapped
    REQUIRE(utf16_to_utf8(swapped) == "ab");
}

TEST_CASE("latin1_to_utf8 maps high bytes to two-byte sequences") {
    string in = "caf\xE9";
    REQUIRE(latin1_to_utf8(in) == "café");
}

TEST_CASE("path_to_utf8 round-trips a non-ASCII name through path_from_utf8") {
    string name = "Straße.wav";
    fs::path path = path_from_utf8(name);
    REQUIRE(path_to_utf8(path) == name);
}

TEST_CASE("path_from_utf8 preserves a non-ASCII filename's bytes when joined under a directory") {
    string filename = "Straße.wav";
    fs::path full = path_from_utf8("music") / path_from_utf8(filename);
    REQUIRE(path_to_utf8(full.filename()) == filename);
}

static const string kReplacement = "\xEF\xBF\xBD";   // U+FFFD in UTF-8

TEST_CASE("sanitize_utf8 returns valid UTF-8 unchanged") {
    for (string text : {"", "plain ASCII", "Måns Zelmerlöw – 東京 🎧",
                          "\xED\x9F\xBF", "\xEE\x80\x80", "\xF4\x8F\xBF\xBF", "\xEF\xBF\xBD"})   // U+D7FF, U+E000, U+10FFFF, U+FFFD
        REQUIRE(sanitize_utf8(text) == text);
}

TEST_CASE("sanitize_utf8 re-decodes text that isn't UTF-8 as Windows-1252") {
    REQUIRE(sanitize_utf8("Beyonc\xE9 Mix") == "Beyoncé Mix");
    REQUIRE(sanitize_utf8("\x80") == "€");
    REQUIRE(sanitize_utf8("\x81") == kReplacement);            // one of cp1252's five undefined bytes
    REQUIRE(sanitize_utf8("\x8D\x8F\x90\x9D") == kReplacement + kReplacement + kReplacement + kReplacement);
    REQUIRE(sanitize_utf8("\x93quoted\x94 \x96 caf\xE9") == "“quoted” – café");
}

TEST_CASE("sanitize_utf8 replaces the FFFE/FFFF noncharacters libFLAC rejects, without falling back to cp1252") {
    REQUIRE(sanitize_utf8("Mix \xEF\xBF\xBF") == "Mix \xEF\xBF\xBD");   // U+FFFF -> U+FFFD
    REQUIRE(sanitize_utf8("Mix \xEF\xBF\xBE") == "Mix \xEF\xBF\xBD");   // U+FFFE -> U+FFFD
    // Valid multi-byte UTF-8 elsewhere in the same string must survive untouched: a cp1252
    // re-read (the fallback for genuinely invalid UTF-8) would mangle "é"'s own bytes.
    REQUIRE(sanitize_utf8("Beyonc\xC3\xA9 Mix \xEF\xBF\xBF") == "Beyonc\xC3\xA9 Mix \xEF\xBF\xBD");
}

TEST_CASE("sanitize_utf8 treats every malformed sequence as invalid (overlong / surrogate / beyond U+10FFFF / bad continuation / truncated)") {
    REQUIRE(sanitize_utf8("\xC0\xAF") == "\xC3\x80\xC2\xAF");                        // overlong "/" -> "À¯"
    REQUIRE(sanitize_utf8("\xED\xA0\x80") == "\xC3\xAD\xC2\xA0\xE2\x82\xAC");        // lone surrogate U+D800 -> "í", NBSP, "€"
    REQUIRE(sanitize_utf8("\xF4\x90\x80\x80") == "\xC3\xB4" + kReplacement + "€€");  // U+110000 -> "ô", U+FFFD, "€€"
    REQUIRE(sanitize_utf8("\xE2\x28\xA1") == "\xC3\xA2(\xC2\xA1");                   // bad continuation -> "â(¡"
    REQUIRE(sanitize_utf8("ab\xE2\x82") == "ab\xC3\xA2\xE2\x80\x9A");                // truncated "€" -> "abâ‚"
}

TEST_CASE("utf8_to_utf16 replaces an invalid byte with U+FFFD and keeps the bytes after it") {
    const char16_t replacement = char16_t(0xFFFD);
    REQUIRE(utf8_to_utf16("a\xE9" "b") == u16string{u'a', replacement, u'b'});
    REQUIRE(utf8_to_utf16("Beyonc\xE9 Mix") == u"Beyonc" + u16string(1, replacement) + u" Mix");
    REQUIRE(utf8_to_utf16("\xE2\x28\xA1") == u16string{replacement, u'(', replacement});
    REQUIRE(utf8_to_utf16("\xC0\xAF") == u16string{replacement, replacement});
    REQUIRE(utf8_to_utf16("\xED\xA0\x80") == u16string{replacement, replacement, replacement});
}

TEST_CASE("utf16_to_utf8 turns an unpaired surrogate into U+FFFD") {
    REQUIRE(utf16_to_utf8(u16string{u'a', char16_t(0xD800), u'b'}) == "a" + kReplacement + "b");
    REQUIRE(utf16_to_utf8(u16string{u'a', char16_t(0xDC00)}) == "a" + kReplacement);
    REQUIRE(utf16_to_utf8(u16string{u'a', char16_t(0xD83C)}) == "a" + kReplacement);   // high surrogate at the end
}
