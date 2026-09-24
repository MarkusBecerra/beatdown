#include "core/content_check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#include "core/decoder.hpp"

namespace fs = std::filesystem;

namespace beatdown {

namespace {

double db20(double linear) { return 20.0 * std::log10(linear); }

std::string db_str(double db) {
    if (!std::isfinite(db)) return db < 0 ? "-inf" : "inf";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", db);
    return buf;
}

// --- FLAC: full frame-by-frame comparison, at whatever precision the source format calls for ---

std::string verify_flac_content(const fs::path& out, const fs::path& source) {
    std::string err;
    auto so = Decoder::open(source, err);
    if (!so) return "audio content: cannot reopen source: " + err;
    auto od = Decoder::open(out, err);
    if (!od) return "audio content: cannot open output: " + err;

    const AudioInfo& si = so->info();
    const AudioInfo& oi = od->info();
    if (oi.channels != si.channels)
        return "audio content: channel count " + std::to_string(oi.channels) + ", expected " + std::to_string(si.channels);
    const int channels = si.channels;
    const int64_t kFrames = 4096;
    int64_t frame_index = 0;

    if (si.is_float) {
        // Float/double sources are written as 24-bit FLAC (libsndfile's ceiling): tolerate the
        // resulting ~1-LSB-at-24-bit rounding. A source can legitimately exceed +-1.0 (an
        // over-full-scale float master); the encoder clips to +-1.0 before quantizing
        // (FlacEncoder sets SFC_SET_CLIPPING), so that clamped value -- not the raw source
        // sample -- is what a correct encode is supposed to produce.
        const double kTolerance = 2.0 / 8388608.0;
        std::vector<float> sbuf(static_cast<size_t>(kFrames) * channels), obuf(static_cast<size_t>(kFrames) * channels);
        for (;;) {
            int64_t sn = so->read_float(sbuf.data(), kFrames);
            int64_t on = od->read_float(obuf.data(), kFrames);
            int64_t n = std::min(sn, on);
            for (int64_t i = 0; i < n; ++i) {
                for (int c = 0; c < channels; ++c) {
                    double a = std::clamp(static_cast<double>(sbuf[static_cast<size_t>(i) * channels + c]), -1.0, 1.0);
                    double b = obuf[static_cast<size_t>(i) * channels + c];
                    if (std::fabs(a - b) > kTolerance)
                        return "audio content: mismatch at frame " + std::to_string(frame_index + i) + ", channel " + std::to_string(c);
                }
            }
            if (sn != on)
                return "audio content: frame count " + std::to_string(frame_index + on) + ", expected " + std::to_string(frame_index + sn);
            if (sn == 0) break;
            frame_index += n;
        }
    } else {
        // 32-bit integer sources are truncated to 24-bit FLAC (libsndfile's ceiling): the low
        // byte can round; <=24-bit sources round-trip bit-exact.
        bool loose = si.bits >= 32;
        std::vector<int32_t> sbuf(static_cast<size_t>(kFrames) * channels), obuf(static_cast<size_t>(kFrames) * channels);
        for (;;) {
            int64_t sn = so->read_int(sbuf.data(), kFrames);
            int64_t on = od->read_int(obuf.data(), kFrames);
            int64_t n = std::min(sn, on);
            for (int64_t i = 0; i < n; ++i) {
                for (int c = 0; c < channels; ++c) {
                    int64_t a = sbuf[static_cast<size_t>(i) * channels + c], b = obuf[static_cast<size_t>(i) * channels + c];
                    bool bad = loose ? (std::llabs(a - b) >= 256) : (a != b);
                    if (bad) return "audio content: mismatch at frame " + std::to_string(frame_index + i) + ", channel " + std::to_string(c);
                }
            }
            if (sn != on)
                return "audio content: frame count " + std::to_string(frame_index + on) + ", expected " + std::to_string(frame_index + sn);
            if (sn == 0) break;
            frame_index += n;
        }
    }
    return "";
}

// --- MP3: aligned, low-pass-filtered RMS level comparison + decoded-length check ---
//
// Task 18 fix round 1: decoded via libsndfile's mpg123-backed "mpeg" feature (through the same
// Decoder as everything else), not LAME's own hip_decode API. mpglib's decode functions
// (hip_decode1_headersB and friends) share a function-static output buffer across every handle
// and are not thread-safe -- concurrent conversions raced on it (wrong peaks, spurious "audio
// content" failures, and a crash inside III_dequantize_sample, per the round-1 review). This
// is also confirmed empirically (see the task report) to decode gaplessly -- it reads the LAME
// tag itself -- so, unlike hip's own decoder, there is no encoder/decoder priming delay to skip:
// alignment starts at sample 0 on both sides.

// A 4th-order (two cascaded biquads, Q 1/sqrt(2)) Butterworth lowpass at 16 kHz, transposed
// Direct Form II. Applied to both sides before every RMS figure below, so the MP3's own
// intentional ~20.3 kHz rolloff (see the club-readiness report) isn't counted as a level
// difference -- full-band material otherwise reads quieter on the decoded side by exactly the
// energy that rolloff removes, which rejected legitimate bright/broadband masters (round-1
// review, Important 2). Left inactive (pass-through) when 16 kHz would be at or past 0.45x this
// side's own Nyquist, where a stable biquad can't be designed (only reachable below ~35.5 kHz).
struct Lowpass16k {
    double z[2][2] = {};  // [stage][state]
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    bool active = false;

    void design(double rate) {
        const double fc = 16000.0;
        if (fc >= 0.45 * rate) return;
        active = true;
        double w0 = 2.0 * 3.14159265358979323846 * fc / rate;
        double cs = std::cos(w0), sn = std::sin(w0);
        double alpha = sn / (2.0 * 0.70710678118654752440);
        double a0 = 1.0 + alpha;
        b0 = (1.0 - cs) / 2.0 / a0;
        b1 = (1.0 - cs) / a0;
        b2 = (1.0 - cs) / 2.0 / a0;
        a1 = (-2.0 * cs) / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double process(double x) {
        if (!active) return x;
        for (int stage = 0; stage < 2; ++stage) {
            double y = b0 * x + z[stage][0];
            z[stage][0] = b1 * x - a1 * y + z[stage][1];
            z[stage][1] = b2 * x - a2 * y;
            x = y;
        }
        return x;
    }
};

// Per-channel, per-1-second-block RMS accumulation (post-lowpass) -- only a double+count per
// second of audio is kept, not the decoded audio itself -- plus the raw (pre-filter) peak and
// the true decoded frame count (from what was actually read back, not the container's declared
// length: for a truncated file that field is the stale original count, confirmed empirically,
// so it wouldn't catch the truncation at all).
struct SideStats {
    int channels = 0, rate = 0;
    int64_t total_frames = 0;
    double peak = 0.0;
    std::vector<double> block_sumsq[2];
    std::vector<int64_t> block_count[2];
    Lowpass16k lp[2];

    void init(int ch, int r) {
        channels = ch;
        rate = r;
        for (int c = 0; c < channels; ++c) lp[c].design(rate);
    }
    void add_frame(const float* frame) {
        int64_t block = total_frames / rate;
        for (int c = 0; c < channels; ++c) {
            double raw = frame[c];
            peak = std::max(peak, std::fabs(raw));
            double v = lp[c].process(raw);
            auto& sums = block_sumsq[c];
            auto& counts = block_count[c];
            if (static_cast<size_t>(block) >= sums.size()) {
                sums.resize(static_cast<size_t>(block) + 1, 0.0);
                counts.resize(static_cast<size_t>(block) + 1, 0);
            }
            sums[static_cast<size_t>(block)] += v * v;
            counts[static_cast<size_t>(block)] += 1;
        }
        ++total_frames;
    }
    int64_t total_count(int c) const {
        int64_t n = 0;
        for (auto v : block_count[c]) n += v;
        return n;
    }
    double total_sumsq(int c) const {
        double s = 0.0;
        for (auto v : block_sumsq[c]) s += v;
        return s;
    }
    double overall_db(int c) const {
        int64_t n = total_count(c);
        return n > 0 ? db20(std::sqrt(total_sumsq(c) / n)) : -std::numeric_limits<double>::infinity();
    }
    size_t num_blocks() const { return block_sumsq[0].size(); }
    double block_db(int c, size_t block) const {
        int64_t n = block_count[c][block];
        return n > 0 ? db20(std::sqrt(block_sumsq[c][block] / static_cast<double>(n))) : -std::numeric_limits<double>::infinity();
    }
};

void accumulate(Decoder& d, SideStats& st) {
    st.init(d.info().channels, d.info().sample_rate);
    const int64_t kFrames = 4096;
    std::vector<float> buf(static_cast<size_t>(kFrames) * st.channels);
    int64_t n;
    while ((n = d.read_float(buf.data(), kFrames)) > 0)
        for (int64_t i = 0; i < n; ++i) st.add_frame(&buf[static_cast<size_t>(i) * st.channels]);
}

ContentCheckResult verify_mp3_content(const fs::path& out, const fs::path& source) {
    ContentCheckResult r;
    std::string err;
    auto sd = Decoder::open(source, err);
    if (!sd) {
        r.error = "audio content: cannot reopen source: " + err;
        return r;
    }
    auto od = Decoder::open(out, err);
    if (!od) {
        r.error = "audio content: cannot open output: " + err;
        return r;
    }

    const AudioInfo& si = sd->info();
    const AudioInfo& oi = od->info();
    if (oi.channels != si.channels) {
        r.error = "audio content: channel count " + std::to_string(oi.channels) + ", expected " + std::to_string(si.channels);
        return r;
    }

    SideStats src, mp3;
    accumulate(*sd, src);
    accumulate(*od, mp3);
    r.peak_dbfs = db20(mp3.peak);

    // Length: decoded frames must equal the source exactly at an unchanged rate, else land
    // within +-2 frames of round(source_frames * out_rate / in_rate).
    bool same_rate = oi.sample_rate == si.sample_rate;
    int64_t expected = same_rate ? si.frames
                                  : std::llround(static_cast<double>(si.frames) * oi.sample_rate / si.sample_rate);
    int64_t diff = std::llabs(mp3.total_frames - expected);
    if (same_rate ? (diff != 0) : (diff > 2)) {
        r.error = "audio content: length " + std::to_string(mp3.total_frames) + " frames, expected " + std::to_string(expected) +
                  (same_rate ? "" : " (±2)");
        return r;
    }

    // (a) per-channel overall RMS within +-0.5 dB; a near-silent source channel (below -60
    // dBFS, where a relative dB comparison is numerically unstable) just needs the decoded
    // channel to also be quiet (below -50 dBFS), not an exact match.
    for (int c = 0; c < si.channels; ++c) {
        double src_db = src.overall_db(c), out_db = mp3.overall_db(c);
        if (src_db < -60.0) {
            if (out_db >= -50.0) {
                r.error = "audio content: channel " + std::to_string(c) + " decoded RMS " + db_str(out_db) +
                          " dBFS, expected below -50.00 dBFS (source is near-silent, " + db_str(src_db) + " dBFS)";
                return r;
            }
        } else if (std::fabs(out_db - src_db) > 0.5) {
            r.error = "audio content: channel " + std::to_string(c) + " RMS " + db_str(out_db) + " dBFS, expected " + db_str(src_db) +
                      " dBFS (±0.5 dB)";
            return r;
        }
    }

    // (b) every aligned 1 s block whose source RMS is above -50 dBFS matches within +-1.0 dB;
    // the first and last block are skipped.
    size_t blocks = std::min(src.num_blocks(), mp3.num_blocks());
    if (blocks >= 3) {
        for (size_t k = 1; k + 1 < blocks; ++k) {
            for (int c = 0; c < si.channels; ++c) {
                double src_db = src.block_db(c, k);
                if (src_db <= -50.0) continue;
                double out_db = mp3.block_db(c, k);
                if (std::fabs(out_db - src_db) > 1.0) {
                    r.error = "audio content: 1 s block " + std::to_string(k) + " channel " + std::to_string(c) + " RMS " +
                              db_str(out_db) + " dBFS, expected " + db_str(src_db) + " dBFS (±1.0 dB)";
                    return r;
                }
            }
        }
    }
    return r;
}

}  // namespace

ContentCheckResult verify_content(const fs::path& out, const EncodeSettings& s, const fs::path& source) {
    if (s.format == Format::Flac) {
        ContentCheckResult r;
        r.error = verify_flac_content(out, source);
        return r;
    }
    return verify_mp3_content(out, source);
}

}  // namespace beatdown
