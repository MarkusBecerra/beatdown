#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/mp3_parse.hpp"
#include "core/id3v2.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("parse_mp3 counts CBR frames and computes duration") {
    TempDir temp_dir;
    string data;
    for (int index = 0; index < 100; ++index) data += build_mp3_frame_for_test(320, 44100, index % 2 == 1, false, nullptr);
    write_bytes(temp_dir.path / "a.mp3", data);
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "a.mp3", info, error_message));
    REQUIRE(info.audio_frames == 100);
    REQUIRE(info.sample_rate == 44100);
    REQUIRE(info.channels == 2);
    REQUIRE(info.cbr(320));
    REQUIRE_FALSE(info.cbr(192));
    REQUIRE(info.duration_seconds() == Catch::Approx(100.0 * 1152 / 44100));
    REQUIRE(info.trailing_bytes == 0);
}

TEST_CASE("parse_mp3 skips a leading ID3v2 tag and reads it") {
    TempDir temp_dir;
    Tags tags; tags.title = "T"; tags.artist = "A";
    string data = build_id3v2_for_test(tags, 3) + build_mp3_frame_for_test(320, 48000, false, false, nullptr);
    write_bytes(temp_dir.path / "a.mp3", data);
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "a.mp3", info, error_message));
    REQUIRE(info.id3v2_size == build_id3v2_for_test(tags, 3).size());
    REQUIRE(info.audio_frames == 1);
    REQUIRE(info.tags.title == "T");
}

TEST_CASE("parse_mp3 recognises an Info/Xing frame and excludes it from audio") {
    TempDir temp_dir;
    string data = build_mp3_frame_for_test(320, 48000, false, false, "Info") + build_mp3_frame_for_test(320, 48000, false, false, nullptr);
    write_bytes(temp_dir.path / "a.mp3", data);
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "a.mp3", info, error_message));
    REQUIRE(info.has_xing);
    REQUIRE(info.audio_frames == 1);
}

TEST_CASE("parse_mp3 sees mixed bitrates as non-CBR and mono as 1 channel") {
    TempDir temp_dir;
    string data = build_mp3_frame_for_test(320, 44100, false, true, nullptr) + build_mp3_frame_for_test(192, 44100, false, true, nullptr);
    write_bytes(temp_dir.path / "a.mp3", data);
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "a.mp3", info, error_message));
    REQUIRE_FALSE(info.cbr(320));
    REQUIRE(info.bitrates.size() == 2);
    REQUIRE(info.channels == 1);
}

TEST_CASE("parse_mp3 reports trailing garbage") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "a.mp3", build_mp3_frame_for_test(320, 44100, false, false, nullptr) + string(300, 'x'));
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "a.mp3", info, error_message));
    REQUIRE(info.trailing_bytes == 300);
}

TEST_CASE("parse_mp3 fails on empty file") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "e.mp3", "");
    Mp3Info info; string error_message;
    REQUIRE_FALSE(parse_mp3(temp_dir.path / "e.mp3", info, error_message));
}

TEST_CASE("parse_mp3 fails on non-MP3 data") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "n.mp3", "not an mp3 at all");
    Mp3Info info; string error_message;
    REQUIRE_FALSE(parse_mp3(temp_dir.path / "n.mp3", info, error_message));
}

TEST_CASE("parse_mp3 handles CRC-protected Info frame") {
    TempDir temp_dir;
    string data = build_mp3_frame_for_test(320, 48000, false, false, "Info", true) + build_mp3_frame_for_test(320, 48000, false, false, nullptr);
    write_bytes(temp_dir.path / "a.mp3", data);
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "a.mp3", info, error_message));
    REQUIRE(info.has_xing);
    REQUIRE(info.audio_frames == 1);
}
