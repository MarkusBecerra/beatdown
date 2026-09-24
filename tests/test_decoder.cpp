#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <vector>
#include "core/decoder.hpp"
#include "core/unicode.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("Decoder reads a 24-bit 48 kHz stereo WAV") {
    TempDir temp_dir;
    auto file = make_audio(temp_dir.path / "a.wav", {.subtype = SF_FORMAT_PCM_24, .rate = 48000, .channels = 2, .seconds = 0.5});
    string error_message;
    auto decoder = Decoder::open(file, error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->info().channels == 2);
    REQUIRE(decoder->info().sample_rate == 48000);
    REQUIRE(decoder->info().frames == 24000);
    REQUIRE(decoder->info().bits == 24);
    REQUIRE_FALSE(decoder->info().is_float);
    REQUIRE(decoder->info().seconds() == Catch::Approx(0.5));
    REQUIRE(decoder->info().pcm_bytes() == 24000 * 2 * 3);
    vector<float> buffer(4096 * 2);
    int64_t total = 0, frames_read;
    while ((frames_read = decoder->read_float(buffer.data(), 4096)) > 0) total += frames_read;
    REQUIRE(total == 24000);
}

TEST_CASE("Decoder reports bit depth for 16/32/float sources") {
    TempDir temp_dir;
    string error_message;
    REQUIRE(Decoder::open(make_audio(temp_dir.path / "16.wav", {.subtype = SF_FORMAT_PCM_16}), error_message)->info().bits == 16);
    REQUIRE(Decoder::open(make_audio(temp_dir.path / "32.wav", {.subtype = SF_FORMAT_PCM_32}), error_message)->info().bits == 32);
    auto decoder = Decoder::open(make_audio(temp_dir.path / "f.wav", {.subtype = SF_FORMAT_FLOAT}), error_message);
    REQUIRE(decoder->info().bits == 32);
    REQUIRE(decoder->info().is_float);
}

TEST_CASE("Decoder opens AIFF, FLAC, mono and 96 kHz") {
    TempDir temp_dir;
    string error_message;
    REQUIRE(Decoder::open(make_audio(temp_dir.path / "a.aiff", {.container = SF_FORMAT_AIFF}), error_message));
    REQUIRE(Decoder::open(make_audio(temp_dir.path / "a.flac", {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16}), error_message));
    auto decoder = Decoder::open(make_audio(temp_dir.path / "m.wav", {.rate = 96000, .channels = 1}), error_message);
    REQUIRE(decoder->info().channels == 1);
    REQUIRE(decoder->info().sample_rate == 96000);
}

TEST_CASE("Decoder opens a non-ASCII path") {
    TempDir temp_dir;
    string error_message;
    auto file = make_audio(temp_dir.path / path_from_utf8("Måns – 東京.wav"));
    REQUIRE(Decoder::open(file, error_message));
}

TEST_CASE("Decoder fails a corrupt file with a message, not a crash") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "bad.wav", kCorruptWav);
    string error_message;
    auto decoder = Decoder::open(temp_dir.path / "bad.wav", error_message);
    REQUIRE_FALSE(decoder);
    REQUIRE_FALSE(error_message.empty());
}

TEST_CASE("Decoder exposes LIST/INFO tags") {
    TempDir temp_dir;
    Tags tags; tags.title = "Deep Cut"; tags.artist = "Some Artist"; tags.genre = "House";
    auto file = make_audio(temp_dir.path / "tagged.wav", {.tags = tags});
    string error_message;
    auto decoder = Decoder::open(file, error_message);
    REQUIRE(decoder->tags().title == "Deep Cut");
    REQUIRE(decoder->tags().artist == "Some Artist");
    REQUIRE(decoder->tags().genre == "House");
    REQUIRE_FALSE(decoder->tags().album.has_value());
}

// sf_set_string stores the bytes as given, so this writes a raw Windows-1252 INFO title.
TEST_CASE("Decoder re-decodes a LIST/INFO tag that isn't UTF-8 as Windows-1252") {
    TempDir temp_dir;
    Tags raw; raw.title = string("Beyonc\xE9 Mix"); raw.artist = "Plain Artist";
    auto file = make_audio(temp_dir.path / "cp1252.wav", {.seconds = 0.1, .tags = raw});
    string error_message;
    auto decoder = Decoder::open(file, error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->tags().title == "Beyoncé Mix");
    REQUIRE(decoder->tags().artist == "Plain Artist");
}

TEST_CASE("Decoder read_int and seek_start") {
    TempDir temp_dir;
    string error_message;
    auto decoder = Decoder::open(make_audio(temp_dir.path / "a.wav", {.subtype = SF_FORMAT_PCM_16, .seconds = 0.1}), error_message);
    vector<int32_t> buffer(4800 * 2);
    REQUIRE(decoder->read_int(buffer.data(), 4800) == 4800);
    REQUIRE(decoder->read_int(buffer.data(), 4800) == 0);
    REQUIRE(decoder->seek_start());
    REQUIRE(decoder->read_int(buffer.data(), 4800) == 4800);
}
