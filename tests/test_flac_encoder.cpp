#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <vector>
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "fixtures.hpp"

using namespace beatdown;

static std::string encode_flac(const fs::path& src, const fs::path& out, Tags tags = {}) {
    std::string err;
    auto d = Decoder::open(src, err);
    REQUIRE(d);
    std::atomic<bool> cancel{false};
    EncodeSettings s; s.format = Format::Flac;
    return make_encoder(s)->encode(*d, out, tags, cancel, nullptr);
}

static std::vector<int32_t> samples(const fs::path& f) {
    std::string err;
    auto d = Decoder::open(f, err);
    REQUIRE(d);
    std::vector<int32_t> all(static_cast<size_t>(d->info().frames) * d->info().channels);
    REQUIRE(d->read_int(all.data(), d->info().frames) == d->info().frames);
    return all;
}

TEST_CASE("FlacEncoder round-trips 24-bit and 16-bit PCM bit-exactly") {
    TempDir t;
    for (int sub : {SF_FORMAT_PCM_24, SF_FORMAT_PCM_16}) {
        auto src = make_audio(t.path / (std::to_string(sub) + ".wav"), {.subtype = sub, .rate = 48000, .seconds = 0.5});
        auto out = t.path / (std::to_string(sub) + ".flac");
        REQUIRE(encode_flac(src, out) == "");
        std::string err;
        auto d = Decoder::open(out, err);
        REQUIRE(d);
        REQUIRE(d->info().sample_rate == 48000);
        REQUIRE(d->info().channels == 2);
        REQUIRE(d->info().frames == 24000);
        REQUIRE(d->info().bits == (sub == SF_FORMAT_PCM_24 ? 24 : 16));
        REQUIRE(samples(src) == samples(out));
    }
}

TEST_CASE("FlacEncoder writes float and 32-bit sources as 24-bit") {
    TempDir t;
    REQUIRE(encode_flac(make_audio(t.path / "f.wav", {.subtype = SF_FORMAT_FLOAT}), t.path / "f.flac") == "");
    std::string err;
    REQUIRE(Decoder::open(t.path / "f.flac", err)->info().bits == 24);
    REQUIRE(encode_flac(make_audio(t.path / "i.wav", {.subtype = SF_FORMAT_PCM_32}), t.path / "i.flac") == "");
    REQUIRE(Decoder::open(t.path / "i.flac", err)->info().bits == 24);
}

TEST_CASE("FlacEncoder writes tags as Vorbis comments and keeps 96 kHz") {
    TempDir t;
    Tags tags; tags.title = "Café"; tags.artist = "A"; tags.genre = "House";
    REQUIRE(encode_flac(make_audio(t.path / "a.wav", {.rate = 96000, .seconds = 0.1}), t.path / "a.flac", tags) == "");
    std::string err;
    auto d = Decoder::open(t.path / "a.flac", err);
    REQUIRE(d->info().sample_rate == 96000);
    REQUIRE(d->tags().title == "Café");
    REQUIRE(d->tags().artist == "A");
    REQUIRE(d->tags().genre == "House");
}

TEST_CASE("FlacEncoder stops when cancelled") {
    TempDir t;
    std::string err;
    auto d = Decoder::open(make_audio(t.path / "l.wav", {.seconds = 10.0}), err);
    std::atomic<bool> cancel{true};
    EncodeSettings s; s.format = Format::Flac;
    REQUIRE(make_encoder(s)->encode(*d, t.path / "l.flac", {}, cancel, nullptr) == "cancelled");
}
