#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "core/verifier.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

static AudioInfo encode(const fs::path& src, const fs::path& out, EncodeSettings settings) {
    string err;
    auto decoder = Decoder::open(src, err);
    REQUIRE(decoder);
    atomic<bool> cancel{false};
    REQUIRE(make_encoder(settings)->encode(*decoder, out, {}, cancel, nullptr) == "");
    return decoder->info();
}

TEST_CASE("verify_mp3 passes a good CBR encode") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 1.0});
    EncodeSettings cbr;
    AudioInfo info = encode(src, temp_dir.path / "cbr.mp3", cbr);
    REQUIRE(verify_mp3(temp_dir.path / "cbr.mp3", cbr, info) == "");
}

TEST_CASE("verify_mp3 passes a good VBR encode") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 1.0});
    EncodeSettings vbr;
    vbr.vbr = 2;
    AudioInfo info = encode(src, temp_dir.path / "vbr.mp3", vbr);
    REQUIRE(verify_mp3(temp_dir.path / "vbr.mp3", vbr, info) == "");
}

TEST_CASE("verify_mp3 fails on the wrong bitrate") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s192;
    s192.bitrate = 192;
    AudioInfo info = encode(src, temp_dir.path / "a.mp3", s192);
    EncodeSettings s320;
    REQUIRE_THAT(verify_mp3(temp_dir.path / "a.mp3", s320, info), ContainsSubstring("bitrate"));
}

TEST_CASE("verify_mp3 fails a truncated file") {
    TempDir temp_dir;
    // R-G: a 4 s source keeps this well clear of the 1 s duration tolerance, so a cut in
    // half cannot land there by accident. The trailing-bytes check runs first, so either
    // "trailing bytes" or "duration" is a legitimate reason for the failure.
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 4.0});
    EncodeSettings s192;
    s192.bitrate = 192;
    AudioInfo info = encode(src, temp_dir.path / "a.mp3", s192);
    string data = read_file(temp_dir.path / "a.mp3");
    write_bytes(temp_dir.path / "cut.mp3", data.substr(0, data.size() / 2));
    REQUIRE_FALSE(verify_mp3(temp_dir.path / "cut.mp3", s192, info).empty());
}

TEST_CASE("verify_mp3 fails a duration mismatch") {
    TempDir temp_dir;
    // R-G: deterministic duration failure, independent of the truncation case above —
    // the file is intact, only the expected source duration is wrong.
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s192;
    s192.bitrate = 192;
    AudioInfo info = encode(src, temp_dir.path / "a.mp3", s192);
    AudioInfo wrong = info;
    wrong.frames *= 3;
    REQUIRE_THAT(verify_mp3(temp_dir.path / "a.mp3", s192, wrong), ContainsSubstring("duration"));
}

TEST_CASE("verify_mp3 fails on a missing file") {
    TempDir temp_dir;
    REQUIRE_THAT(verify_mp3(temp_dir.path / "missing.mp3", EncodeSettings{}, AudioInfo{}), ContainsSubstring("missing"));
}

TEST_CASE("verify_mp3 fails an implausibly small file") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "tiny.mp3", "xx");
    REQUIRE_FALSE(verify_mp3(temp_dir.path / "tiny.mp3", EncodeSettings{}, AudioInfo{}).empty());
}

TEST_CASE("verify_mp3 checks the sample rate rule") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.rate = 96000, .seconds = 0.5});
    EncodeSettings settings;
    AudioInfo info = encode(src, temp_dir.path / "a.mp3", settings);
    REQUIRE(verify_mp3(temp_dir.path / "a.mp3", settings, info) == "");
    AudioInfo wrong = info;
    wrong.sample_rate = 44100;
    REQUIRE_THAT(verify_mp3(temp_dir.path / "a.mp3", settings, wrong), ContainsSubstring("sample rate"));
}

TEST_CASE("verify_flac and verify_output pass a good FLAC encode") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 0.5});
    EncodeSettings settings;
    settings.format = Format::Flac;
    AudioInfo info = encode(src, temp_dir.path / "a.flac", settings);
    REQUIRE(verify_flac(temp_dir.path / "a.flac", info) == "");
    REQUIRE(verify_output(temp_dir.path / "a.flac", settings, info) == "");
}

TEST_CASE("verify_flac fails a frame-count mismatch") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 0.5});
    EncodeSettings settings;
    settings.format = Format::Flac;
    AudioInfo info = encode(src, temp_dir.path / "a.flac", settings);
    AudioInfo wrong = info;
    wrong.frames += 10;
    REQUIRE_THAT(verify_flac(temp_dir.path / "a.flac", wrong), ContainsSubstring("frames"));
}

TEST_CASE("verify_flac fails an unreadable file") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "bad.flac", "fLaCjunk");
    REQUIRE_FALSE(verify_flac(temp_dir.path / "bad.flac", AudioInfo{}).empty());
}
