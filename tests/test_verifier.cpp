#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "core/verifier.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

static AudioInfo encode(const fs::path& src, const fs::path& out, EncodeSettings s) {
    std::string err;
    auto d = Decoder::open(src, err);
    REQUIRE(d);
    std::atomic<bool> cancel{false};
    REQUIRE(make_encoder(s)->encode(*d, out, {}, cancel, nullptr) == "");
    return d->info();
}

TEST_CASE("verify_mp3 passes a good CBR encode") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 1.0});
    EncodeSettings cbr;
    AudioInfo a = encode(src, t.path / "cbr.mp3", cbr);
    REQUIRE(verify_mp3(t.path / "cbr.mp3", cbr, a) == "");
}

TEST_CASE("verify_mp3 passes a good VBR encode") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 1.0});
    EncodeSettings vbr;
    vbr.vbr = 2;
    AudioInfo a = encode(src, t.path / "vbr.mp3", vbr);
    REQUIRE(verify_mp3(t.path / "vbr.mp3", vbr, a) == "");
}

TEST_CASE("verify_mp3 fails on the wrong bitrate") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s192;
    s192.bitrate = 192;
    AudioInfo a = encode(src, t.path / "a.mp3", s192);
    EncodeSettings s320;
    REQUIRE_THAT(verify_mp3(t.path / "a.mp3", s320, a), ContainsSubstring("bitrate"));
}

TEST_CASE("verify_mp3 fails a truncated file") {
    TempDir t;
    // R-G: a 4 s source keeps this well clear of the 1 s duration tolerance, so a cut in
    // half cannot land there by accident. The trailing-bytes check runs first, so either
    // "trailing bytes" or "duration" is a legitimate reason for the failure.
    auto src = make_audio(t.path / "a.wav", {.seconds = 4.0});
    EncodeSettings s192;
    s192.bitrate = 192;
    AudioInfo a = encode(src, t.path / "a.mp3", s192);
    std::string data = read_file(t.path / "a.mp3");
    write_bytes(t.path / "cut.mp3", data.substr(0, data.size() / 2));
    REQUIRE_FALSE(verify_mp3(t.path / "cut.mp3", s192, a).empty());
}

TEST_CASE("verify_mp3 fails a duration mismatch") {
    TempDir t;
    // R-G: deterministic duration failure, independent of the truncation case above —
    // the file is intact, only the expected source duration is wrong.
    auto src = make_audio(t.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s192;
    s192.bitrate = 192;
    AudioInfo a = encode(src, t.path / "a.mp3", s192);
    AudioInfo wrong = a;
    wrong.frames *= 3;
    REQUIRE_THAT(verify_mp3(t.path / "a.mp3", s192, wrong), ContainsSubstring("duration"));
}

TEST_CASE("verify_mp3 fails on a missing file") {
    TempDir t;
    REQUIRE_THAT(verify_mp3(t.path / "missing.mp3", EncodeSettings{}, AudioInfo{}), ContainsSubstring("missing"));
}

TEST_CASE("verify_mp3 fails an implausibly small file") {
    TempDir t;
    write_bytes(t.path / "tiny.mp3", "xx");
    REQUIRE_FALSE(verify_mp3(t.path / "tiny.mp3", EncodeSettings{}, AudioInfo{}).empty());
}

TEST_CASE("verify_mp3 checks the sample rate rule") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.rate = 96000, .seconds = 0.5});
    EncodeSettings s;
    AudioInfo a = encode(src, t.path / "a.mp3", s);
    REQUIRE(verify_mp3(t.path / "a.mp3", s, a) == "");
    AudioInfo wrong = a;
    wrong.sample_rate = 44100;
    REQUIRE_THAT(verify_mp3(t.path / "a.mp3", s, wrong), ContainsSubstring("sample rate"));
}

TEST_CASE("verify_flac and verify_output pass a good FLAC encode") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 0.5});
    EncodeSettings s;
    s.format = Format::Flac;
    AudioInfo a = encode(src, t.path / "a.flac", s);
    REQUIRE(verify_flac(t.path / "a.flac", a) == "");
    REQUIRE(verify_output(t.path / "a.flac", s, a) == "");
}

TEST_CASE("verify_flac fails a frame-count mismatch") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 0.5});
    EncodeSettings s;
    s.format = Format::Flac;
    AudioInfo a = encode(src, t.path / "a.flac", s);
    AudioInfo wrong = a;
    wrong.frames += 10;
    REQUIRE_THAT(verify_flac(t.path / "a.flac", wrong), ContainsSubstring("frames"));
}

TEST_CASE("verify_flac fails an unreadable file") {
    TempDir t;
    write_bytes(t.path / "bad.flac", "fLaCjunk");
    REQUIRE_FALSE(verify_flac(t.path / "bad.flac", AudioInfo{}).empty());
}
