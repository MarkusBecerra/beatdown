#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <vector>
#include "core/decoder.hpp"
#include "core/unicode.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("Decoder reads a 24-bit 48 kHz stereo WAV") {
    TempDir t;
    auto f = make_audio(t.path / "a.wav", {.subtype = SF_FORMAT_PCM_24, .rate = 48000, .channels = 2, .seconds = 0.5});
    std::string err;
    auto d = Decoder::open(f, err);
    REQUIRE(d);
    REQUIRE(d->info().channels == 2);
    REQUIRE(d->info().sample_rate == 48000);
    REQUIRE(d->info().frames == 24000);
    REQUIRE(d->info().bits == 24);
    REQUIRE_FALSE(d->info().is_float);
    REQUIRE(d->info().seconds() == Catch::Approx(0.5));
    REQUIRE(d->info().pcm_bytes() == 24000 * 2 * 3);
    std::vector<float> buf(4096 * 2);
    int64_t total = 0, n;
    while ((n = d->read_float(buf.data(), 4096)) > 0) total += n;
    REQUIRE(total == 24000);
}

TEST_CASE("Decoder reports bit depth for 16/32/float sources") {
    TempDir t;
    std::string err;
    REQUIRE(Decoder::open(make_audio(t.path / "16.wav", {.subtype = SF_FORMAT_PCM_16}), err)->info().bits == 16);
    REQUIRE(Decoder::open(make_audio(t.path / "32.wav", {.subtype = SF_FORMAT_PCM_32}), err)->info().bits == 32);
    auto f = Decoder::open(make_audio(t.path / "f.wav", {.subtype = SF_FORMAT_FLOAT}), err);
    REQUIRE(f->info().bits == 32);
    REQUIRE(f->info().is_float);
}

TEST_CASE("Decoder opens AIFF, FLAC, mono and 96 kHz") {
    TempDir t;
    std::string err;
    REQUIRE(Decoder::open(make_audio(t.path / "a.aiff", {.container = SF_FORMAT_AIFF}), err));
    REQUIRE(Decoder::open(make_audio(t.path / "a.flac", {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16}), err));
    auto m = Decoder::open(make_audio(t.path / "m.wav", {.rate = 96000, .channels = 1}), err);
    REQUIRE(m->info().channels == 1);
    REQUIRE(m->info().sample_rate == 96000);
}

TEST_CASE("Decoder opens a non-ASCII path") {
    TempDir t;
    std::string err;
    auto f = make_audio(t.path / path_from_utf8("Måns – 東京.wav"));
    REQUIRE(Decoder::open(f, err));
}

TEST_CASE("Decoder fails a corrupt file with a message, not a crash") {
    TempDir t;
    write_bytes(t.path / "bad.wav", kCorruptWav);
    std::string err;
    auto d = Decoder::open(t.path / "bad.wav", err);
    REQUIRE_FALSE(d);
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("Decoder exposes LIST/INFO tags") {
    TempDir t;
    Tags tags; tags.title = "Deep Cut"; tags.artist = "Some Artist"; tags.genre = "House";
    auto f = make_audio(t.path / "tagged.wav", {.tags = tags});
    std::string err;
    auto d = Decoder::open(f, err);
    REQUIRE(d->tags().title == "Deep Cut");
    REQUIRE(d->tags().artist == "Some Artist");
    REQUIRE(d->tags().genre == "House");
    REQUIRE_FALSE(d->tags().album.has_value());
}

TEST_CASE("Decoder read_int and seek_start") {
    TempDir t;
    std::string err;
    auto d = Decoder::open(make_audio(t.path / "a.wav", {.subtype = SF_FORMAT_PCM_16, .seconds = 0.1}), err);
    std::vector<int32_t> buf(4800 * 2);
    REQUIRE(d->read_int(buf.data(), 4800) == 4800);
    REQUIRE(d->read_int(buf.data(), 4800) == 0);
    REQUIRE(d->seek_start());
    REQUIRE(d->read_int(buf.data(), 4800) == 4800);
}
