#include <catch2/catch_test_macros.hpp>
#include "core/space.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("estimate_output_bytes for MP3 is duration x bitrate") {
    AudioInfo a; a.sample_rate = 48000; a.frames = 48000 * 60; a.channels = 2; a.bits = 24;
    EncodeSettings s;
    REQUIRE(estimate_output_bytes(a, s) == 60 * 320 * 125);
    s.bitrate = 192;
    REQUIRE(estimate_output_bytes(a, s) == 60 * 192 * 125);
    s.vbr = 0;
    REQUIRE(estimate_output_bytes(a, s) == 60 * 256 * 125);
}

TEST_CASE("estimate_output_bytes for FLAC is 70% of PCM") {
    AudioInfo a; a.sample_rate = 48000; a.frames = 48000; a.channels = 2; a.bits = 24;
    EncodeSettings s; s.format = Format::Flac;
    REQUIRE(estimate_output_bytes(a, s) == static_cast<int64_t>(0.7 * 48000 * 2 * 3));
}

TEST_CASE("check_space applies a 10% margin") {
    REQUIRE(check_space(1000, 1100).ok);
    REQUIRE_FALSE(check_space(1000, 1099).ok);
    SpaceCheck c = check_space(1000, 500);
    REQUIRE(c.needed == 1100);
    REQUIRE(c.available == 500);
}

TEST_CASE("available_bytes reports space for a real directory and errors for a missing one") {
    TempDir t;
    string err;
    REQUIRE(available_bytes(t.path, err) > 0);
    REQUIRE(err.empty());
    available_bytes(t.path / "nope", err);
    REQUIRE_FALSE(err.empty());
}
