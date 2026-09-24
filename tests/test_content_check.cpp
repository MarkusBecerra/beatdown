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

void encode_to(const fs::path& src, const fs::path& out, EncodeSettings s) {
    std::string err;
    auto d = Decoder::open(src, err);
    REQUIRE(d);
    std::atomic<bool> cancel{false};
    REQUIRE(make_encoder(s)->encode(*d, out, {}, cancel, nullptr) == "");
}

// Writes a mono signal duplicated into both channels, mirroring fixtures.cpp's make_audio.
fs::path write_signal(const fs::path& file, int rate, const std::vector<double>& mono) {
    fs::create_directories(file.parent_path());
    SF_INFO info{};
    info.samplerate = rate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    SNDFILE* sf = beatdown::platform::sf_open_path(file, SFM_WRITE, &info);
    if (!sf) throw std::runtime_error("write_signal: open failed");
    std::vector<float> buf(mono.size() * 2);
    for (size_t i = 0; i < mono.size(); ++i) buf[i * 2] = buf[i * 2 + 1] = static_cast<float>(mono[i]);
    sf_writef_float(sf, buf.data(), static_cast<sf_count_t>(mono.size()));
    sf_close(sf);
    return file;
}

// A hard-clipped, square-ish -0.1 dBFS wave: a true square wave (a real value discontinuity, not
// just a rounded clipped-sine corner). Band-limiting a discontinuity like this is exactly what
// causes Gibbs-phenomenon ringing, which overshoots the original level by a fixed fraction of the
// jump regardless of how many harmonics survive the encoder's lowpass -- the mechanism behind the
// "loud master" decoded-peak reporting exists to catch.
fs::path make_clipped_wave(const fs::path& file, double seconds = 1.0, int rate = 48000) {
    const double clip = amp_for_dbfs(-0.1);
    int64_t n = static_cast<int64_t>(rate * seconds);
    std::vector<double> x(n);
    for (int64_t i = 0; i < n; ++i) x[i] = std::sin(2.0 * kPi * 440.0 * i / rate) >= 0.0 ? clip : -clip;
    return write_signal(file, rate, x);
}

// A minimal RBJ high-pass biquad, cascaded twice (4th order), used only to build "bright"
// (high-frequency-heavy) test material below: a self-contained re-derivation of the same
// technique the round-1 review's own probe generator used, not a dependency on it.
struct Biquad {
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double process(double x) {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};
Biquad make_highpass(double rate, double fc, double q = 0.70710678118654752440) {
    Biquad bq;
    double w0 = 2.0 * kPi * fc / rate, cs = std::cos(w0), sn = std::sin(w0);
    double alpha = sn / (2.0 * q), a0 = 1.0 + alpha;
    bq.b0 = (1.0 + cs) / 2.0 / a0;
    bq.b1 = -(1.0 + cs) / a0;
    bq.b2 = (1.0 + cs) / 2.0 / a0;
    bq.a1 = (-2.0 * cs) / a0;
    bq.a2 = (1.0 - alpha) / a0;
    return bq;
}
std::vector<double> highpass4(double rate, double fc, std::vector<double> xs) {
    Biquad a = make_highpass(rate, fc), b = make_highpass(rate, fc);
    for (auto& x : xs) x = b.process(a.process(x));
    return xs;
}
double rms_of(const std::vector<double>& xs) {
    double s = 0.0;
    for (double v : xs) s += v * v;
    return std::sqrt(s / std::max<size_t>(1, xs.size()));
}

std::vector<double> gen_white(int rate, double seconds, double rms_db, unsigned seed = 12345) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, 1.0);
    std::vector<double> x(static_cast<size_t>(rate * seconds));
    for (auto& v : x) v = dist(rng);
    double k = amp_for_dbfs(rms_db) / rms_of(x);
    for (auto& v : x) v *= k;
    return x;
}

// A 1 kHz sine at `base_db` peak, with [start, start+len) replaced by high-passed noise at
// `block_db` RMS -- a fixed-frequency stand-in for a "riser" block of bright material.
std::vector<double> gen_riser_block(int rate, double seconds, double base_db, double block_db, double fc,
                                     double start_s, double len_s) {
    int64_t n = static_cast<int64_t>(rate * seconds);
    double A = amp_for_dbfs(base_db);
    std::vector<double> x(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) x[static_cast<size_t>(i)] = A * std::sin(2.0 * kPi * 1000.0 * i / rate);
    int64_t st = static_cast<int64_t>(rate * start_s), ln = static_cast<int64_t>(rate * len_s);
    std::mt19937 rng(777);
    std::normal_distribution<double> dist(0.0, 1.0);
    std::vector<double> noise(static_cast<size_t>(ln));
    for (auto& v : noise) v = dist(rng);
    noise = highpass4(rate, fc, noise);
    double k = amp_for_dbfs(block_db) / rms_of(noise);
    for (int64_t i = 0; i < ln && st + i < n; ++i) x[static_cast<size_t>(st + i)] = noise[static_cast<size_t>(i)] * k;
    return x;
}

// A quiet high-passed noise "hat" intro (RMS `intro_db`) followed by a louder 1 kHz sine.
std::vector<double> gen_hp_intro(int rate, double intro_s, double total_s, double intro_db, double fc, double loud_db) {
    int64_t n = static_cast<int64_t>(rate * total_s), intro = static_cast<int64_t>(rate * intro_s);
    std::mt19937 rng(999);
    std::normal_distribution<double> dist(0.0, 1.0);
    std::vector<double> hp(static_cast<size_t>(intro));
    for (auto& v : hp) v = dist(rng);
    hp = highpass4(rate, fc, hp);
    double k = amp_for_dbfs(intro_db) / rms_of(hp);
    std::vector<double> x(static_cast<size_t>(n));
    for (int64_t i = 0; i < intro; ++i) x[static_cast<size_t>(i)] = hp[static_cast<size_t>(i)] * k;
    double A = amp_for_dbfs(loud_db);
    for (int64_t i = intro; i < n; ++i) x[static_cast<size_t>(i)] = A * std::sin(2.0 * kPi * 1000.0 * (i - intro) / rate);
    return x;
}

// A decaying percussive one-shot: 150 -> 50 Hz sweep under an exponential amplitude decay.
std::vector<double> gen_kick(int rate, double seconds, double peak_db, double tau) {
    int64_t n = static_cast<int64_t>(rate * seconds);
    double A = amp_for_dbfs(peak_db);
    std::vector<double> x(static_cast<size_t>(n));
    double phase = 0.0;
    for (int64_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / rate;
        double f = 50.0 + 100.0 * std::exp(-t / 0.02);
        phase += 2.0 * kPi * f / rate;
        x[static_cast<size_t>(i)] = A * std::exp(-t / tau) * std::sin(phase);
    }
    return x;
}
}  // namespace

TEST_CASE("verify_content passes a good MP3 encode") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src).error == "");
}

TEST_CASE("verify_content passes a good FLAC encode (24-bit, 16-bit, float32 and int32 sources)") {
    TempDir t;
    EncodeSettings s;
    s.format = Format::Flac;
    for (int sub : {SF_FORMAT_PCM_24, SF_FORMAT_PCM_16, SF_FORMAT_FLOAT, SF_FORMAT_PCM_32}) {
        auto src = make_audio(t.path / (std::to_string(sub) + ".wav"), {.subtype = sub, .seconds = 0.5});
        auto out = t.path / (std::to_string(sub) + ".flac");
        encode_to(src, out, s);
        REQUIRE(verify_content(out, s, src).error == "");
    }
}

TEST_CASE("verify_content fails an MP3 of silence checked against a non-silent sine source") {
    TempDir t;
    auto silent = make_audio(t.path / "silent.wav", {.seconds = 1.0, .amplitude = 0.0});
    auto real = make_audio(t.path / "real.wav", {.seconds = 1.0, .amplitude = 0.5});
    EncodeSettings s;
    encode_to(silent, t.path / "out.mp3", s);
    REQUIRE_THAT(verify_content(t.path / "out.mp3", s, real).error, ContainsSubstring("audio content"));
}

TEST_CASE("verify_content fails an MP3 encoded from a -20 dBFS sine checked against a -6 dBFS sine source") {
    TempDir t;
    auto quiet = make_audio(t.path / "quiet.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-20.0)});
    auto loud = make_audio(t.path / "loud.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-6.0)});
    EncodeSettings s;
    encode_to(quiet, t.path / "out.mp3", s);
    REQUIRE_THAT(verify_content(t.path / "out.mp3", s, loud).error, ContainsSubstring("audio content"));
}

TEST_CASE("verify_content fails a FLAC of a different signal checked against the source and names a frame") {
    TempDir t;
    auto a = make_audio(t.path / "a.wav", {.seconds = 0.5, .amplitude = 0.5});
    auto b = make_audio(t.path / "b.wav", {.seconds = 0.5, .amplitude = 0.2});
    EncodeSettings s;
    s.format = Format::Flac;
    encode_to(a, t.path / "a.flac", s);
    auto r = verify_content(t.path / "a.flac", s, b);
    REQUIRE_THAT(r.error, ContainsSubstring("audio content"));
    REQUIRE_THAT(r.error, ContainsSubstring("frame"));
}

TEST_CASE("verify_content passes a 96 kHz source encoded to MP3") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.rate = 96000, .seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src).error == "");
}

TEST_CASE("verify_content passes a 22.05 kHz source encoded to MP3") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.rate = 22050, .seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src).error == "");
}

TEST_CASE("verify_content passes a mono source encoded to MP3") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.channels = 1, .seconds = 2.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    REQUIRE(verify_content(t.path / "a.mp3", s, src).error == "");
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
    REQUIRE(verify_content(t.path / "a.mp3", s, src).error == "");
}

// --- Fix round 1 additions ---

TEST_CASE("verify_content reports the decoded peak of a -20 dBFS 1 kHz sine within 0.3 dB") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 1.0, .amplitude = amp_for_dbfs(-20.0), .freq_hz = 1000.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    auto r = verify_content(t.path / "a.mp3", s, src);
    REQUIRE(r.error == "");
    REQUIRE(r.peak_dbfs.has_value());
    REQUIRE(*r.peak_dbfs == Catch::Approx(-20.0).margin(0.3));
}

TEST_CASE("verify_content reports a decoded peak above 0 dBFS for a hot, hard-clipped square-ish wave") {
    TempDir t;
    auto src = make_clipped_wave(t.path / "hot.wav");
    EncodeSettings s;
    encode_to(src, t.path / "hot.mp3", s);
    auto r = verify_content(t.path / "hot.mp3", s, src);
    REQUIRE(r.peak_dbfs.has_value());
    REQUIRE(*r.peak_dbfs > 0.0);
}

TEST_CASE("verify_content reports no decoded peak for FLAC") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 0.2});
    EncodeSettings s;
    s.format = Format::Flac;
    encode_to(src, t.path / "a.flac", s);
    auto r = verify_content(t.path / "a.flac", s, src);
    REQUIRE(r.error == "");
    REQUIRE_FALSE(r.peak_dbfs.has_value());
}

TEST_CASE("verify_content passes a float +3 dBFS sine encoded to both formats") {
    TempDir t;
    auto src = make_audio(t.path / "hot.wav", {.subtype = SF_FORMAT_FLOAT, .seconds = 1.0, .amplitude = amp_for_dbfs(3.0)});

    EncodeSettings mp3s;
    encode_to(src, t.path / "hot.mp3", mp3s);
    REQUIRE(verify_content(t.path / "hot.mp3", mp3s, src).error == "");

    EncodeSettings flacs;
    flacs.format = Format::Flac;
    encode_to(src, t.path / "hot.flac", flacs);
    REQUIRE(verify_content(t.path / "hot.flac", flacs, src).error == "");
}

TEST_CASE("verify_content fails a truncated MP3 with a length mismatch") {
    TempDir t;
    auto src = make_audio(t.path / "a.wav", {.seconds = 3.0});
    EncodeSettings s;
    encode_to(src, t.path / "a.mp3", s);
    std::string data = read_file(t.path / "a.mp3");
    write_bytes(t.path / "cut.mp3", data.substr(0, data.size() * 2 / 3));
    auto r = verify_content(t.path / "cut.mp3", s, src);
    REQUIRE_THAT(r.error, ContainsSubstring("audio content"));
    REQUIRE_THAT(r.error, ContainsSubstring("length"));
}

TEST_CASE("verify_content passes a decaying 0.1 s one-shot") {
    TempDir t;
    auto src = write_signal(t.path / "kick.wav", 48000, gen_kick(48000, 0.1, -1.0, 0.03));
    EncodeSettings s;
    encode_to(src, t.path / "kick.mp3", s);
    REQUIRE(verify_content(t.path / "kick.mp3", s, src).error == "");
}

// Round-1 review, Important 2: full-band RMS (without a shared lowpass) rejected legitimate
// bright/broadband material because the MP3's own ~20.3 kHz intentional rolloff removed energy
// the full-band RMS still counted. These three reproduce the reviewer's probe categories.
TEST_CASE("verify_content passes 48 kHz white noise") {
    TempDir t;
    auto src = write_signal(t.path / "white.wav", 48000, gen_white(48000, 3.0, -20.0));
    EncodeSettings s;
    encode_to(src, t.path / "white.mp3", s);
    REQUIRE(verify_content(t.path / "white.mp3", s, src).error == "");
}

TEST_CASE("verify_content passes a source with a high-passed riser block") {
    TempDir t;
    auto src = write_signal(t.path / "riser.wav", 48000, gen_riser_block(48000, 6.0, -10.0, -20.0, 10000.0, 2.0, 2.0));
    EncodeSettings s;
    encode_to(src, t.path / "riser.mp3", s);
    REQUIRE(verify_content(t.path / "riser.mp3", s, src).error == "");
}

TEST_CASE("verify_content passes a -48 dBFS high-passed hat intro followed by louder content") {
    TempDir t;
    auto src = write_signal(t.path / "intro.wav", 48000, gen_hp_intro(48000, 4.0, 8.0, -48.0, 8000.0, -10.0));
    EncodeSettings s;
    encode_to(src, t.path / "intro.mp3", s);
    REQUIRE(verify_content(t.path / "intro.mp3", s, src).error == "");
}
