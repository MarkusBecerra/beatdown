#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>
#include <sndfile.h>
#include "core/content_check.hpp"
#include "core/decoder.hpp"
#include "core/encoder.hpp"
#include "core/platform/platform.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

namespace {
constexpr double kPi = 3.14159265358979323846;

void encode_to(const fs::path& src, const fs::path& out, EncodeSettings settings) {
    string err;
    auto decoder = Decoder::open(src, err);
    REQUIRE(decoder);
    atomic<bool> cancel{false};
    REQUIRE(make_encoder(settings)->encode(*decoder, out, {}, cancel, nullptr) == "");
}

// Writes a mono signal duplicated into both channels, mirroring fixtures.cpp's make_audio.
fs::path write_signal(const fs::path& file, int rate, const vector<double>& mono) {
    fs::create_directories(file.parent_path());
    SF_INFO info{};
    info.samplerate = rate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* sndfile = beatdown::platform::sf_open_path(file, SFM_WRITE, &info);
    if (!sndfile) throw runtime_error("write_signal: open failed");
    vector<float> buf(mono.size() * 2);
    for (size_t index = 0; index < mono.size(); ++index) buf[index * 2] = buf[index * 2 + 1] = static_cast<float>(mono[index]);
    sf_writef_float(sndfile, buf.data(), static_cast<sf_count_t>(mono.size()));
    sf_close(sndfile);
    return file;
}

// A hard-clipped, square-ish -0.1 dBFS wave: a true square wave (a real value discontinuity, not
// just a rounded clipped-sine corner). Band-limiting a discontinuity like this is exactly what
// causes Gibbs-phenomenon ringing, which overshoots the original level by a fixed fraction of the
// jump regardless of how many harmonics survive the encoder's lowpass -- the mechanism behind the
// "loud master" decoded-peak reporting exists to catch.
fs::path make_clipped_wave(const fs::path& file, double seconds = 1.0, int rate = 48000) {
    const double clip = amp_for_dbfs(-0.1);
    int64_t frame_count = static_cast<int64_t>(rate * seconds);
    vector<double> samples(frame_count);
    for (int64_t index = 0; index < frame_count; ++index) samples[index] = sin(2.0 * kPi * 440.0 * index / rate) >= 0.0 ? clip : -clip;
    return write_signal(file, rate, samples);
}

// A minimal RBJ high-pass biquad, cascaded twice (4th order), used only to build "bright"
// (high-frequency-heavy) test material below: a self-contained re-derivation of the same
// technique the round-1 review's own probe generator used, not a dependency on it.
struct Biquad {
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double process(double sample) {
        double filtered = b0 * sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = sample; y2 = y1; y1 = filtered;
        return filtered;
    }
};
Biquad make_highpass(double rate, double cutoff_hz, double q_factor = 0.70710678118654752440) {
    Biquad filter;
    double w0 = 2.0 * kPi * cutoff_hz / rate, cosine = cos(w0), sine = sin(w0);
    double alpha = sine / (2.0 * q_factor), a0 = 1.0 + alpha;
    filter.b0 = (1.0 + cosine) / 2.0 / a0;
    filter.b1 = -(1.0 + cosine) / a0;
    filter.b2 = (1.0 + cosine) / 2.0 / a0;
    filter.a1 = (-2.0 * cosine) / a0;
    filter.a2 = (1.0 - alpha) / a0;
    return filter;
}
vector<double> highpass4(double rate, double cutoff_hz, vector<double> xs) {
    Biquad stage1 = make_highpass(rate, cutoff_hz), stage2 = make_highpass(rate, cutoff_hz);
    for (auto& sample : xs) sample = stage2.process(stage1.process(sample));
    return xs;
}
double rms_of(const vector<double>& xs) {
    double sum_of_squares = 0.0;
    for (double value : xs) sum_of_squares += value * value;
    return sqrt(sum_of_squares / std::max<size_t>(1, xs.size()));
}

vector<double> gen_white(int rate, double seconds, double rms_db, unsigned seed = 12345) {
    mt19937 rng(seed);
    normal_distribution<double> dist(0.0, 1.0);
    vector<double> samples(static_cast<size_t>(rate * seconds));
    for (auto& sample : samples) sample = dist(rng);
    double scale = amp_for_dbfs(rms_db) / rms_of(samples);
    for (auto& sample : samples) sample *= scale;
    return samples;
}

// A 1 kHz sine at `base_db` peak, with [start, start+len) replaced by high-passed noise at
// `block_db` RMS -- a fixed-frequency stand-in for a "riser" block of bright material.
vector<double> gen_riser_block(int rate, double seconds, double base_db, double block_db, double cutoff_hz,
                                     double start_s, double len_s) {
    int64_t frame_count = static_cast<int64_t>(rate * seconds);
    double amplitude = amp_for_dbfs(base_db);
    vector<double> samples(static_cast<size_t>(frame_count));
    for (int64_t index = 0; index < frame_count; ++index) samples[static_cast<size_t>(index)] = amplitude * sin(2.0 * kPi * 1000.0 * index / rate);
    int64_t start_frame = static_cast<int64_t>(rate * start_s), length_frames = static_cast<int64_t>(rate * len_s);
    mt19937 rng(777);
    normal_distribution<double> dist(0.0, 1.0);
    vector<double> noise(static_cast<size_t>(length_frames));
    for (auto& sample : noise) sample = dist(rng);
    noise = highpass4(rate, cutoff_hz, noise);
    double scale = amp_for_dbfs(block_db) / rms_of(noise);
    for (int64_t index = 0; index < length_frames && start_frame + index < frame_count; ++index) samples[static_cast<size_t>(start_frame + index)] = noise[static_cast<size_t>(index)] * scale;
    return samples;
}

// A quiet high-passed noise "hat" intro (RMS `intro_db`) followed by a louder 1 kHz sine.
vector<double> gen_hp_intro(int rate, double intro_s, double total_s, double intro_db, double cutoff_hz, double loud_db) {
    int64_t frame_count = static_cast<int64_t>(rate * total_s), intro = static_cast<int64_t>(rate * intro_s);
    mt19937 rng(999);
    normal_distribution<double> dist(0.0, 1.0);
    vector<double> intro_noise(static_cast<size_t>(intro));
    for (auto& sample : intro_noise) sample = dist(rng);
    intro_noise = highpass4(rate, cutoff_hz, intro_noise);
    double scale = amp_for_dbfs(intro_db) / rms_of(intro_noise);
    vector<double> samples(static_cast<size_t>(frame_count));
    for (int64_t index = 0; index < intro; ++index) samples[static_cast<size_t>(index)] = intro_noise[static_cast<size_t>(index)] * scale;
    double amplitude = amp_for_dbfs(loud_db);
    for (int64_t index = intro; index < frame_count; ++index) samples[static_cast<size_t>(index)] = amplitude * sin(2.0 * kPi * 1000.0 * (index - intro) / rate);
    return samples;
}

// A decaying percussive one-shot: 150 -> 50 Hz sweep under an exponential amplitude decay.
vector<double> gen_kick(int rate, double seconds, double peak_db, double tau) {
    int64_t frame_count = static_cast<int64_t>(rate * seconds);
    double amplitude = amp_for_dbfs(peak_db);
    vector<double> samples(static_cast<size_t>(frame_count));
    double phase = 0.0;
    for (int64_t index = 0; index < frame_count; ++index) {
        double time = static_cast<double>(index) / rate;
        double freq = 50.0 + 100.0 * exp(-time / 0.02);
        phase += 2.0 * kPi * freq / rate;
        samples[static_cast<size_t>(index)] = amplitude * exp(-time / tau) * sin(phase);
    }
    return samples;
}
}  // namespace

TEST_CASE("verify_content passes a good MP3 encode") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 2.0});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "a.mp3", settings, src).error == "");
}

TEST_CASE("verify_content passes a good FLAC encode (24-bit, 16-bit, float32 and int32 sources)") {
    TempDir temp_dir;
    EncodeSettings settings;
    settings.format = Format::Flac;
    for (int subtype : {SF_FORMAT_PCM_24, SF_FORMAT_PCM_16, SF_FORMAT_FLOAT, SF_FORMAT_PCM_32}) {
        auto src = make_audio(temp_dir.path / (to_string(subtype) + ".wav"), {.subtype = subtype, .seconds = 0.5});
        auto out = temp_dir.path / (to_string(subtype) + ".flac");
        encode_to(src, out, settings);
        REQUIRE(verify_content(out, settings, src).error == "");
    }
}

TEST_CASE("verify_content fails an MP3 of silence checked against a non-silent sine source") {
    TempDir temp_dir;
    auto silent = make_audio(temp_dir.path / "silent.wav", {.seconds = 1.0, .amplitude = 0.0});
    auto real = make_audio(temp_dir.path / "real.wav", {.seconds = 1.0, .amplitude = 0.5});
    EncodeSettings settings;
    encode_to(silent, temp_dir.path / "out.mp3", settings);
    REQUIRE_THAT(verify_content(temp_dir.path / "out.mp3", settings, real).error, ContainsSubstring("audio content"));
}

TEST_CASE("verify_content fails an MP3 encoded from a -20 dBFS sine checked against a -6 dBFS sine source") {
    TempDir temp_dir;
    auto quiet = make_audio(temp_dir.path / "quiet.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-20.0)});
    auto loud = make_audio(temp_dir.path / "loud.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-6.0)});
    EncodeSettings settings;
    encode_to(quiet, temp_dir.path / "out.mp3", settings);
    REQUIRE_THAT(verify_content(temp_dir.path / "out.mp3", settings, loud).error, ContainsSubstring("audio content"));
}

TEST_CASE("verify_content fails a FLAC of a different signal checked against the source and names a frame") {
    TempDir temp_dir;
    auto original = make_audio(temp_dir.path / "a.wav", {.seconds = 0.5, .amplitude = 0.5});
    auto different = make_audio(temp_dir.path / "b.wav", {.seconds = 0.5, .amplitude = 0.2});
    EncodeSettings settings;
    settings.format = Format::Flac;
    encode_to(original, temp_dir.path / "a.flac", settings);
    auto result = verify_content(temp_dir.path / "a.flac", settings, different);
    REQUIRE_THAT(result.error, ContainsSubstring("audio content"));
    REQUIRE_THAT(result.error, ContainsSubstring("frame"));
}

TEST_CASE("verify_content passes a 96 kHz source encoded to MP3") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.rate = 96000, .seconds = 2.0});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "a.mp3", settings, src).error == "");
}

TEST_CASE("verify_content passes a 22.05 kHz source encoded to MP3") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.rate = 22050, .seconds = 2.0});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "a.mp3", settings, src).error == "");
}

TEST_CASE("verify_content passes a mono source encoded to MP3") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.channels = 1, .seconds = 2.0});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "a.mp3", settings, src).error == "");
}

// R-G (self-review addition): check (a)'s near-silent branch (source channel RMS below -60
// dBFS requires the decoded RMS below -50 dBFS, rather than the normal +-0.5 dB relative
// tolerance, which is numerically unstable at very low levels). Not in the brief's list but
// cheap to cover since the branch is otherwise untested by any listed case.
TEST_CASE("verify_content allows a near-silent source channel to decode below -50 dBFS instead of matching exactly") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 2.0, .amplitude = amp_for_dbfs(-70.0)});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "a.mp3", settings, src).error == "");
}

// --- Fix round 1 additions ---

TEST_CASE("verify_content reports the decoded peak of a -20 dBFS 1 kHz sine within 0.3 dB") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-20.0), .freq_hz = 1000.0});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    auto result = verify_content(temp_dir.path / "a.mp3", settings, src);
    REQUIRE(result.error == "");
    REQUIRE(result.peak_dbfs.has_value());
    REQUIRE(*result.peak_dbfs == Catch::Approx(-20.0).margin(0.3));
}

TEST_CASE("verify_content reports a decoded peak above 0 dBFS for a hot, hard-clipped square-ish wave") {
    TempDir temp_dir;
    auto src = make_clipped_wave(temp_dir.path / "hot.wav");
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "hot.mp3", settings);
    auto result = verify_content(temp_dir.path / "hot.mp3", settings, src);
    REQUIRE(result.peak_dbfs.has_value());
    REQUIRE(*result.peak_dbfs > 0.0);
}

TEST_CASE("verify_content reports no decoded peak for FLAC") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 0.2});
    EncodeSettings settings;
    settings.format = Format::Flac;
    encode_to(src, temp_dir.path / "a.flac", settings);
    auto result = verify_content(temp_dir.path / "a.flac", settings, src);
    REQUIRE(result.error == "");
    REQUIRE_FALSE(result.peak_dbfs.has_value());
}

TEST_CASE("verify_content passes a float +3 dBFS sine encoded to both formats") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "hot.wav", {.subtype = SF_FORMAT_FLOAT, .seconds = 1.0, .amplitude = amp_for_dbfs(3.0)});

    EncodeSettings mp3s;
    encode_to(src, temp_dir.path / "hot.mp3", mp3s);
    REQUIRE(verify_content(temp_dir.path / "hot.mp3", mp3s, src).error == "");

    EncodeSettings flacs;
    flacs.format = Format::Flac;
    encode_to(src, temp_dir.path / "hot.flac", flacs);
    REQUIRE(verify_content(temp_dir.path / "hot.flac", flacs, src).error == "");
}

TEST_CASE("verify_content fails a truncated MP3 with a length mismatch") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "a.wav", {.seconds = 3.0});
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "a.mp3", settings);
    string data = read_file(temp_dir.path / "a.mp3");
    write_bytes(temp_dir.path / "cut.mp3", data.substr(0, data.size() * 2 / 3));
    auto result = verify_content(temp_dir.path / "cut.mp3", settings, src);
    REQUIRE_THAT(result.error, ContainsSubstring("audio content"));
    REQUIRE_THAT(result.error, ContainsSubstring("length"));
}

TEST_CASE("verify_content passes a decaying 0.1 s one-shot") {
    TempDir temp_dir;
    auto src = write_signal(temp_dir.path / "kick.wav", 48000, gen_kick(48000, 0.1, -1.0, 0.03));
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "kick.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "kick.mp3", settings, src).error == "");
}

// Round-1 review, Important 2: full-band RMS (without a shared lowpass) rejected legitimate
// bright/broadband material because the MP3's own ~20.3 kHz intentional rolloff removed energy
// the full-band RMS still counted. These three reproduce the reviewer's probe categories.
TEST_CASE("verify_content passes 48 kHz white noise") {
    TempDir temp_dir;
    auto src = write_signal(temp_dir.path / "white.wav", 48000, gen_white(48000, 3.0, -20.0));
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "white.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "white.mp3", settings, src).error == "");
}

TEST_CASE("verify_content passes a source with a high-passed riser block") {
    TempDir temp_dir;
    auto src = write_signal(temp_dir.path / "riser.wav", 48000, gen_riser_block(48000, 6.0, -10.0, -20.0, 10000.0, 2.0, 2.0));
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "riser.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "riser.mp3", settings, src).error == "");
}

TEST_CASE("verify_content passes a -48 dBFS high-passed hat intro followed by louder content") {
    TempDir temp_dir;
    auto src = write_signal(temp_dir.path / "intro.wav", 48000, gen_hp_intro(48000, 4.0, 8.0, -48.0, 8000.0, -10.0));
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "intro.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "intro.mp3", settings, src).error == "");
}

// Task 18 fix round 2, item 7: a shared analysis lowpass cutoff (min(16 kHz, 0.4x the lower of
// the two sides' rates)) is used for both sides, so a 32 kHz source (Nyquist 16 kHz) isn't left
// completely unfiltered on one side while the other gets the full 16 kHz lowpass.
TEST_CASE("verify_content passes bright 32 kHz white noise") {
    TempDir temp_dir;
    auto src = write_signal(temp_dir.path / "white32.wav", 32000, gen_white(32000, 3.0, -20.0));
    EncodeSettings settings;
    encode_to(src, temp_dir.path / "white32.mp3", settings);
    REQUIRE(verify_content(temp_dir.path / "white32.mp3", settings, src).error == "");
}
