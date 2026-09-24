#include <catch2/catch_test_macros.hpp>
#include "core/space.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("estimate_output_bytes for MP3 is duration x bitrate") {
    AudioInfo info; info.sample_rate = 48000; info.frames = 48000 * 60; info.channels = 2; info.bits = 24;
    EncodeSettings settings;
    REQUIRE(estimate_output_bytes(info, settings) == 60 * 320 * 125);
    settings.bitrate = 192;
    REQUIRE(estimate_output_bytes(info, settings) == 60 * 192 * 125);
    settings.vbr = 0;
    REQUIRE(estimate_output_bytes(info, settings) == 60 * 256 * 125);
}

TEST_CASE("estimate_output_bytes for FLAC is 70% of PCM") {
    AudioInfo info; info.sample_rate = 48000; info.frames = 48000; info.channels = 2; info.bits = 24;
    EncodeSettings settings; settings.format = Format::Flac;
    REQUIRE(estimate_output_bytes(info, settings) == static_cast<int64_t>(0.7 * 48000 * 2 * 3));
}

TEST_CASE("check_space applies a 10% margin") {
    REQUIRE(check_space(1000, 1100).ok);
    REQUIRE_FALSE(check_space(1000, 1099).ok);
    SpaceCheck check = check_space(1000, 500);
    REQUIRE(check.needed == 1100);
    REQUIRE(check.available == 500);
}

TEST_CASE("available_bytes reports space for a real directory and errors for a missing one") {
    TempDir temp_dir;
    string error_message;
    REQUIRE(available_bytes(temp_dir.path, error_message) > 0);
    REQUIRE(error_message.empty());
    available_bytes(temp_dir.path / "nope", error_message);
    REQUIRE_FALSE(error_message.empty());
}
