#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include "core/content_check.hpp"
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

static void encode_to(const fs::path& src, const fs::path& out, EncodeSettings s) {
    std::string err;
    auto d = Decoder::open(src, err);
    REQUIRE(d);
    std::atomic<bool> cancel{false};
    REQUIRE(make_encoder(s)->encode(*d, out, {}, cancel, nullptr) == "");
}

TEST_CASE("verify_content passes a good MP3 encode") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src) == "");
}

TEST_CASE("verify_content passes a good FLAC encode (24-bit, 16-bit, float32 and int32 sources)") {
    TempDir t;
    EncodeSettings s;
    s.format = Format::Flac;
    for (int sub : {SF_FORMAT_PCM_24, SF_FORMAT_PCM_16, SF_FORMAT_FLOAT, SF_FORMAT_PCM_32}) {
        auto src = make_audio(t.path / (std::to_string(sub) + ".wav"), {.subtype = sub, .seconds = 0.5});
        auto out = t.path / (std::to_string(sub) + ".flac");
        encode_to(src, out, s);
        REQUIRE(verify_content(out, s, src) == "");
    }
}

TEST_CASE("verify_content fails an MP3 of silence checked against a non-silent sine source") {
    TempDir t;
    auto silent = make_audio(t.path / "silent.wav", {.seconds = 1.0, .amplitude = 0.0});
    auto real = make_audio(t.path / "real.wav", {.seconds = 1.0, .amplitude = 0.5});
    EncodeSettings s;
    encode_to(silent, t.path / "out.mp3", s);
    REQUIRE_THAT(verify_content(t.path / "out.mp3", s, real), ContainsSubstring("audio content"));
}

TEST_CASE("verify_content fails an MP3 encoded from a -20 dBFS sine checked against a -6 dBFS sine source") {
    TempDir t;
    auto quiet = make_audio(t.path / "quiet.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-20.0)});
    auto loud = make_audio(t.path / "loud.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-6.0)});
    EncodeSettings s;
    encode_to(quiet, t.path / "out.mp3", s);
    REQUIRE_THAT(verify_content(t.path / "out.mp3", s, loud), ContainsSubstring("audio content"));
}

TEST_CASE("verify_content fails a FLAC of a different signal checked against the source and names a frame") {
    TempDir t;
    auto a = make_audio(t.path / "a.wav", {.seconds = 0.5, .amplitude = 0.5});
    auto b = make_audio(t.path / "b.wav", {.seconds = 0.5, .amplitude = 0.2});
    EncodeSettings s;
    s.format = Format::Flac;
    encode_to(a, t.path / "a.flac", s);
    std::string msg = verify_content(t.path / "a.flac", s, b);
    REQUIRE_THAT(msg, ContainsSubstring("audio content"));
    REQUIRE_THAT(msg, ContainsSubstring("frame"));
}

TEST_CASE("verify_content passes a 96 kHz source encoded to MP3") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.rate = 96000, .seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src) == "");
}

TEST_CASE("verify_content passes a 22.05 kHz source encoded to MP3") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.rate = 22050, .seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src) == "");
}

TEST_CASE("verify_content passes a mono source encoded to MP3") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.channels = 1, .seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src) == "");
}

// R-G (self-review addition): check (a)'s near-silent branch (source channel RMS below -60
// dBFS requires the decoded RMS below -50 dBFS, rather than the normal +-0.5 dB relative
// tolerance, which is numerically unstable at very low levels). Not in the brief's list but
// cheap to cover since the branch is otherwise untested by any listed case.
TEST_CASE("verify_content allows a near-silent source channel to decode below -50 dBFS instead of matching exactly") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 2.0, .amplitude = amp_for_dbfs(-70.0)});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src) == "");
}
