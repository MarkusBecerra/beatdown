#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/mp3_parse.hpp"
#include "core/id3v2.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("parse_mp3 counts CBR frames and computes duration") {
    TempDir t;
    std::string data;
    for (int i = 0; i < 100; ++i) data += build_mp3_frame_for_test(320, 44100, i % 2 == 1, false, nullptr);
    write_bytes(t.path / "a.mp3", data);
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "a.mp3", info, err));
    REQUIRE(info.audio_frames == 100);
    REQUIRE(info.sample_rate == 44100);
    REQUIRE(info.channels == 2);
    REQUIRE(info.cbr(320));
    REQUIRE_FALSE(info.cbr(192));
    REQUIRE(info.duration_seconds() == Catch::Approx(100.0 * 1152 / 44100));
    REQUIRE(info.trailing_bytes == 0);
}

TEST_CASE("parse_mp3 skips a leading ID3v2 tag and reads it") {
    TempDir t;
    Tags tags; tags.title = "T"; tags.artist = "A";
    std::string data = build_id3v2_for_test(tags, 3) + build_mp3_frame_for_test(320, 48000, false, false, nullptr);
    write_bytes(t.path / "a.mp3", data);
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "a.mp3", info, err));
    REQUIRE(info.id3v2_size == build_id3v2_for_test(tags, 3).size());
    REQUIRE(info.audio_frames == 1);
    REQUIRE(info.tags.title == "T");
}

TEST_CASE("parse_mp3 recognises an Info/Xing frame and excludes it from audio") {
    TempDir t;
    std::string data = build_mp3_frame_for_test(320, 48000, false, false, "Info") + build_mp3_frame_for_test(320, 48000, false, false, nullptr);
    write_bytes(t.path / "a.mp3", data);
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "a.mp3", info, err));
    REQUIRE(info.has_xing);
    REQUIRE(info.audio_frames == 1);
}

TEST_CASE("parse_mp3 sees mixed bitrates as non-CBR and mono as 1 channel") {
    TempDir t;
    std::string data = build_mp3_frame_for_test(320, 44100, false, true, nullptr) + build_mp3_frame_for_test(192, 44100, false, true, nullptr);
    write_bytes(t.path / "a.mp3", data);
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "a.mp3", info, err));
    REQUIRE_FALSE(info.cbr(320));
    REQUIRE(info.bitrates.size() == 2);
    REQUIRE(info.channels == 1);
}

TEST_CASE("parse_mp3 reports trailing garbage and fails on empty or non-MP3 data") {
    TempDir t;
    write_bytes(t.path / "a.mp3", build_mp3_frame_for_test(320, 44100, false, false, nullptr) + std::string(300, 'x'));
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "a.mp3", info, err));
    REQUIRE(info.trailing_bytes == 300);
    write_bytes(t.path / "e.mp3", "");
    REQUIRE_FALSE(parse_mp3(t.path / "e.mp3", info, err));
    write_bytes(t.path / "n.mp3", "not an mp3 at all");
    REQUIRE_FALSE(parse_mp3(t.path / "n.mp3", info, err));
}
