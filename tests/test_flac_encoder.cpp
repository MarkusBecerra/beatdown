#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "fixtures.hpp"

using namespace beatdown;

static string encode_flac(const fs::path& src, const fs::path& out, Tags tags = {}) {
    string err;
    auto decoder = Decoder::open(src, err);
    REQUIRE(decoder);
    atomic<bool> cancel{false};
    EncodeSettings settings; settings.format = Format::Flac;
    return make_encoder(settings)->encode(*decoder, out, tags, cancel, nullptr);
}

static vector<int32_t> samples(const fs::path& file) {
    string err;
    auto decoder = Decoder::open(file, err);
    REQUIRE(decoder);
    vector<int32_t> all(static_cast<size_t>(decoder->info().frames) * decoder->info().channels);
    REQUIRE(decoder->read_int(all.data(), decoder->info().frames) == decoder->info().frames);
    return all;
}

TEST_CASE("FlacEncoder round-trips 24-bit and 16-bit PCM bit-exactly") {
    TempDir temp_dir;
    for (int subtype : {SF_FORMAT_PCM_24, SF_FORMAT_PCM_16}) {
        auto src = make_audio(temp_dir.path / (to_string(subtype) + ".wav"), {.subtype = subtype, .rate = 48000, .seconds = 0.5});
        auto out = temp_dir.path / (to_string(subtype) + ".flac");
        REQUIRE(encode_flac(src, out) == "");
        string err;
        auto decoder = Decoder::open(out, err);
        REQUIRE(decoder);
        REQUIRE(decoder->info().sample_rate == 48000);
        REQUIRE(decoder->info().channels == 2);
        REQUIRE(decoder->info().frames == 24000);
        REQUIRE(decoder->info().bits == (subtype == SF_FORMAT_PCM_24 ? 24 : 16));
        REQUIRE(samples(src) == samples(out));
    }
}

static vector<float> float_samples(const fs::path& file) {
    string err;
    auto decoder = Decoder::open(file, err);
    REQUIRE(decoder);
    vector<float> all(static_cast<size_t>(decoder->info().frames) * decoder->info().channels);
    REQUIRE(decoder->read_float(all.data(), decoder->info().frames) == decoder->info().frames);
    return all;
}

// Decoded content, not just the header: a silent FLAC has the right bit depth and frame count too.
TEST_CASE("FlacEncoder writes float and 32-bit sources as 24-bit with the source's content") {
    TempDir temp_dir;
    for (int subtype : {SF_FORMAT_FLOAT, SF_FORMAT_PCM_32}) {
        auto src = make_audio(temp_dir.path / (to_string(subtype) + ".wav"), {.subtype = subtype, .seconds = 0.5});
        auto out = temp_dir.path / (to_string(subtype) + ".flac");
        REQUIRE(encode_flac(src, out) == "");
        string err;
        REQUIRE(Decoder::open(out, err)->info().bits == 24);
        vector<float> want = float_samples(src), got = float_samples(out);
        REQUIRE(got.size() == want.size());
        float peak = 0.0f, worst = 0.0f;
        for (size_t index = 0; index < want.size(); ++index) {
            peak = std::max(peak, fabs(want[index]));
            worst = std::max(worst, fabs(got[index] - want[index]));
        }
        REQUIRE(peak > 0.49f);                     // the fixture is a 0.5-amplitude sine, not silence
        REQUIRE(worst <= 2.0f / 8388608.0f);       // about one 24-bit LSB plus rounding
    }
}

TEST_CASE("FlacEncoder writes tags as Vorbis comments and keeps 96 kHz") {
    TempDir temp_dir;
    Tags tags; tags.title = "Café"; tags.artist = "A"; tags.genre = "House";
    REQUIRE(encode_flac(make_audio(temp_dir.path / "a.wav", {.rate = 96000, .seconds = 0.1}), temp_dir.path / "a.flac", tags) == "");
    string err;
    auto decoder = Decoder::open(temp_dir.path / "a.flac", err);
    REQUIRE(decoder->info().sample_rate == 96000);
    REQUIRE(decoder->tags().title == "Café");
    REQUIRE(decoder->tags().artist == "A");
    REQUIRE(decoder->tags().genre == "House");
}

TEST_CASE("FlacEncoder stops when cancelled") {
    TempDir temp_dir;
    string err;
    auto decoder = Decoder::open(make_audio(temp_dir.path / "l.wav", {.seconds = 10.0}), err);
    atomic<bool> cancel{true};
    EncodeSettings settings; settings.format = Format::Flac;
    REQUIRE(make_encoder(settings)->encode(*decoder, temp_dir.path / "l.flac", {}, cancel, nullptr) == "cancelled");
}
