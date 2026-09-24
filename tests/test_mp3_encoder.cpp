#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include <thread>
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "core/mp3_encoder.hpp"
#include "core/mp3_parse.hpp"
#include "core/verifier.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

static Mp3Info encode_and_parse(const fs::path& src, const fs::path& out, EncodeSettings s = {}, Tags tags = {}) {
    string err;
    auto d = Decoder::open(src, err);
    REQUIRE(d);
    atomic<bool> cancel{false};
    auto enc = make_encoder(s);
    string log;
    REQUIRE(enc->encode(*d, out, tags, cancel, &log) == "");
    Mp3Info info;
    REQUIRE(parse_mp3(out, info, err));
    return info;
}

TEST_CASE("mp3_output_rate keeps 44.1/48 kHz and maps lower rates to 44.1 and higher ones to 48") {
    REQUIRE(mp3_output_rate(44100) == 44100);
    REQUIRE(mp3_output_rate(48000) == 48000);
    REQUIRE(mp3_output_rate(96000) == 48000);
    REQUIRE(mp3_output_rate(88200) == 48000);
    REQUIRE(mp3_output_rate(32000) == 44100);
    REQUIRE(mp3_output_rate(22050) == 44100);
}

// Left to itself LAME would pick MPEG-2 at 22.05 kHz, where 160 kbps is the ceiling, and the
// file would only fail verification after a full encode.
TEST_CASE("LameEncoder encodes a 22.05 kHz source as CBR 320 at 44.1 kHz and it verifies") {
    TempDir t;
    auto src = make_audio(t.path / "22k.wav", {.rate = 22050, .seconds = 1.0});
    Mp3Info info = encode_and_parse(src, t.path / "22k.mp3");
    REQUIRE(info.sample_rate == 44100);
    REQUIRE(info.cbr(320));
    string err;
    auto d = Decoder::open(src, err);
    REQUIRE(d);
    REQUIRE(verify_mp3(t.path / "22k.mp3", EncodeSettings{}, d->info()) == "");
}

TEST_CASE("LameEncoder refuses a CBR bitrate LAME would change and writes nothing") {
    TempDir t;
    string err;
    auto d = Decoder::open(make_audio(t.path / "a.wav", {.seconds = 0.2}), err);
    REQUIRE(d);
    atomic<bool> cancel{false};
    EncodeSettings s; s.bitrate = 8;   // an MPEG-2 rate: MPEG-1 at 48 kHz starts at 32 kbps
    REQUIRE_THAT(make_encoder(s)->encode(*d, t.path / "a.mp3", {}, cancel, nullptr), ContainsSubstring("CBR 8 kbps"));
    REQUIRE_FALSE(fs::exists(t.path / "a.mp3"));
}

TEST_CASE("LameEncoder writes CBR 320 at 48 kHz with the source duration") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.rate = 48000, .seconds = 2.0});
    Mp3Info info = encode_and_parse(src, t.path / "a.mp3");
    REQUIRE(info.cbr(320));
    REQUIRE(info.sample_rate == 48000);
    REQUIRE(info.channels == 2);
    REQUIRE(info.has_xing);
    REQUIRE(info.duration_seconds() == Catch::Approx(2.0).margin(0.2));
    REQUIRE(info.trailing_bytes == 0);
}

TEST_CASE("LameEncoder downsamples 96 kHz to 48 kHz and keeps 44.1") {
    TempDir t;
    REQUIRE(encode_and_parse(make_audio(t.path / "96.wav", {.rate = 96000}), t.path / "96.mp3").sample_rate == 48000);
    REQUIRE(encode_and_parse(make_audio(t.path / "44.wav", {.rate = 44100}), t.path / "44.mp3").sample_rate == 44100);
}

// Task 18 fix round 2, item 5: the output MP3 buffer must be sized for the resample ratio, not
// just the input frame count -- an 8/12 kHz source upsampled to 44.1 kHz packs far more encoded
// output time into the same input chunk than a same-rate encode does.
TEST_CASE("LameEncoder converts 8 kHz and 12 kHz 1-second sources") {
    TempDir t;
    REQUIRE(encode_and_parse(make_audio(t.path / "8k.wav", {.rate = 8000, .seconds = 1.0}), t.path / "8k.mp3").sample_rate == 44100);
    REQUIRE(encode_and_parse(make_audio(t.path / "12k.wav", {.rate = 12000, .seconds = 1.0}), t.path / "12k.mp3").sample_rate == 44100);
}

TEST_CASE("LameEncoder honours --bitrate and --vbr") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s; s.bitrate = 192;
    REQUIRE(encode_and_parse(src, t.path / "192.mp3", s).cbr(192));
    EncodeSettings v; v.vbr = 0;
    Mp3Info info = encode_and_parse(src, t.path / "v0.mp3", v);
    REQUIRE(info.has_xing);
    REQUIRE(info.audio_frames > 0);
}

TEST_CASE("LameEncoder encodes mono and float sources") {
    TempDir t;
    REQUIRE(encode_and_parse(make_audio(t.path / "m.wav", {.channels = 1}), t.path / "m.mp3").channels == 1);
    REQUIRE(encode_and_parse(make_audio(t.path / "f.wav", {.subtype = SF_FORMAT_FLOAT}), t.path / "f.mp3").cbr(320));
}

TEST_CASE("LameEncoder writes ID3v2 tags including non-ASCII text") {
    TempDir t;
    Tags tags; tags.title = "Café Track"; tags.artist = "Some Artist"; tags.album = "Drop"; tags.date = "2026"; tags.track = "7"; tags.genre = "House"; tags.comment = "unreleased";
    Mp3Info info = encode_and_parse(make_audio(t.path / "a.wav", {.seconds = 0.2}), t.path / "a.mp3", {}, tags);
    REQUIRE(info.id3v2_size > 0);
    REQUIRE(info.tags.title == "Café Track");
    REQUIRE(info.tags.artist == "Some Artist");
    REQUIRE(info.tags.album == "Drop");
    REQUIRE(info.tags.date == "2026");
    REQUIRE(info.tags.track == "7");
    REQUIRE(info.tags.genre == "House");
    REQUIRE(info.tags.comment == "unreleased");
}

TEST_CASE("LameEncoder writes no tag block when there are no tags") {
    TempDir t;
    Mp3Info info = encode_and_parse(make_audio(t.path / "a.wav", {.seconds = 0.2}), t.path / "a.mp3");
    REQUIRE(info.id3v2_size == 0);
}

TEST_CASE("LameEncoder stops when cancelled") {
    TempDir t;
    auto src = make_audio(t.path / "long.wav", {.seconds = 30.0});
    string err;
    auto d = Decoder::open(src, err);
    atomic<bool> cancel{true};
    auto enc = make_encoder({});
    REQUIRE(enc->encode(*d, t.path / "long.mp3", {}, cancel, nullptr) == "cancelled");
}

TEST_CASE("LameEncoder refuses more than two channels") {
    TempDir t;
    auto src = make_audio(t.path / "4ch.wav", {.channels = 4, .seconds = 0.1});
    string err;
    auto d = Decoder::open(src, err);
    atomic<bool> cancel{false};
    REQUIRE(make_encoder({})->encode(*d, t.path / "4ch.mp3", {}, cancel, nullptr) != "");
}

