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
    std::u16string le = u"﻿abc";
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
