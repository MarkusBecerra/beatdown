#include "core/content_check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
#include "core/decoder.hpp"
#include "core/std_names.hpp"

namespace beatdown {

namespace {

double linear_to_db(double linear) { return 20.0 * log10(linear); }

string db_str(double db) {
    if (!isfinite(db)) return db < 0 ? "-inf" : "inf";
    char formatted[32];
    snprintf(formatted, sizeof formatted, "%.2f", db);
    return formatted;
}

// --- FLAC: full frame-by-frame comparison, at whatever precision the source format calls for ---

string verify_flac_content(const fs::path& out, const fs::path& source) {
    string error_message;
    auto source_decoder = Decoder::open(source, error_message);
    if (!source_decoder) return "audio content: cannot reopen source: " + error_message;
    auto output_decoder = Decoder::open(out, error_message);
    if (!output_decoder) return "audio content: cannot open output: " + error_message;

    const AudioInfo& source_info = source_decoder->info();
    const AudioInfo& output_info = output_decoder->info();
    if (output_info.channels != source_info.channels)
        return "audio content: channel count " + to_string(output_info.channels) + ", expected " + to_string(source_info.channels);
    const int channels = source_info.channels;
    const int64_t kFrames = 4096;
    int64_t frame_index = 0;

    if (source_info.is_float) {
        // Float/double sources are written as 24-bit FLAC (libsndfile's ceiling): tolerate the
        // resulting ~1-LSB-at-24-bit rounding. A source can legitimately exceed +-1.0 (an
        // over-full-scale float master); the encoder clips to +-1.0 before quantizing
        // (FlacEncoder sets SFC_SET_CLIPPING), so that clamped value -- not the raw source
        // sample -- is what a correct encode is supposed to produce.
        const double kTolerance = 2.0 / 8388608.0;
        vector<float> source_buf(static_cast<size_t>(kFrames) * channels), output_buf(static_cast<size_t>(kFrames) * channels);
        for (;;) {
            int64_t source_frames_read = source_decoder->read_float(source_buf.data(), kFrames);
            int64_t output_frames_read = output_decoder->read_float(output_buf.data(), kFrames);
            int64_t frames_to_compare = std::min(source_frames_read, output_frames_read);
            for (int64_t offset = 0; offset < frames_to_compare; ++offset) {
                for (int channel = 0; channel < channels; ++channel) {
                    double source_sample = std::clamp(static_cast<double>(source_buf[static_cast<size_t>(offset) * channels + channel]), -1.0, 1.0);
                    double output_sample = output_buf[static_cast<size_t>(offset) * channels + channel];
                    if (fabs(source_sample - output_sample) > kTolerance)
                        return "audio content: mismatch at frame " + to_string(frame_index + offset) + ", channel " + to_string(channel);
                }
            }
            if (source_frames_read != output_frames_read)
                return "audio content: frame count " + to_string(frame_index + output_frames_read) + ", expected " + to_string(frame_index + source_frames_read);
            if (source_frames_read == 0) break;
            frame_index += frames_to_compare;
        }
    } else {
        // 32-bit integer sources are truncated to 24-bit FLAC (libsndfile's ceiling): the low
        // byte can round; <=24-bit sources round-trip bit-exact.
        bool loose = source_info.bits >= 32;
        vector<int32_t> source_buf(static_cast<size_t>(kFrames) * channels), output_buf(static_cast<size_t>(kFrames) * channels);
        for (;;) {
            int64_t source_frames_read = source_decoder->read_int(source_buf.data(), kFrames);
            int64_t output_frames_read = output_decoder->read_int(output_buf.data(), kFrames);
            int64_t frames_to_compare = std::min(source_frames_read, output_frames_read);
            for (int64_t offset = 0; offset < frames_to_compare; ++offset) {
                for (int channel = 0; channel < channels; ++channel) {
                    int64_t source_sample = source_buf[static_cast<size_t>(offset) * channels + channel], output_sample = output_buf[static_cast<size_t>(offset) * channels + channel];
                    bool bad = loose ? (std::llabs(source_sample - output_sample) >= 256) : (source_sample != output_sample);
                    if (bad) return "audio content: mismatch at frame " + to_string(frame_index + offset) + ", channel " + to_string(channel);
                }
            }
            if (source_frames_read != output_frames_read)
                return "audio content: frame count " + to_string(frame_index + output_frames_read) + ", expected " + to_string(frame_index + source_frames_read);
            if (source_frames_read == 0) break;
            frame_index += frames_to_compare;
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

// A 4th-order (two cascaded biquads, Q 1/sqrt(2)) Butterworth lowpass at a shared cutoff,
// transposed Direct Form II. Applied to both sides (at the same cutoff frequency, though each
// side runs it at its own sample rate) before every RMS figure below, so the MP3's own
// intentional ~20.3 kHz rolloff (see the club-readiness report) isn't counted as a level
// difference -- full-band material otherwise reads quieter on the decoded side by exactly the
// energy that rolloff removes, which rejected legitimate bright/broadband masters (round-1
// review, Important 2). Left inactive (pass-through) when the cutoff would be at or past 0.45x
// this side's own Nyquist, where a stable biquad can't be designed.
struct Lowpass {
    double delay[2][2] = {};  // [stage][state]
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    bool active = false;

    void design(double rate, double cutoff_hz) {
        if (cutoff_hz >= 0.45 * rate) return;
        active = true;
        double w0 = 2.0 * 3.14159265358979323846 * cutoff_hz / rate;
        double cosine = cos(w0), sine = sin(w0);
        double alpha = sine / (2.0 * 0.70710678118654752440);
        double a0 = 1.0 + alpha;
        b0 = (1.0 - cosine) / 2.0 / a0;
        b1 = (1.0 - cosine) / a0;
        b2 = (1.0 - cosine) / 2.0 / a0;
        a1 = (-2.0 * cosine) / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double process(double sample) {
        if (!active) return sample;
        for (int stage = 0; stage < 2; ++stage) {
            double filtered = b0 * sample + delay[stage][0];
            delay[stage][0] = b1 * sample - a1 * filtered + delay[stage][1];
            delay[stage][1] = b2 * sample - a2 * filtered;
            sample = filtered;
        }
        return sample;
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
    vector<double> block_sumsq[2];
    vector<int64_t> block_count[2];
    Lowpass lowpass[2];

    void init(int channel_count, int sample_rate, double cutoff_hz) {
        channels = channel_count;
        rate = sample_rate;
        for (int channel = 0; channel < channels; ++channel) lowpass[channel].design(rate, cutoff_hz);
    }
    void add_frame(const float* frame) {
        int64_t block = total_frames / rate;
        for (int channel = 0; channel < channels; ++channel) {
            double raw = frame[channel];
            peak = std::max(peak, fabs(raw));
            double filtered = lowpass[channel].process(raw);
            auto& sums = block_sumsq[channel];
            auto& counts = block_count[channel];
            if (static_cast<size_t>(block) >= sums.size()) {
                sums.resize(static_cast<size_t>(block) + 1, 0.0);
                counts.resize(static_cast<size_t>(block) + 1, 0);
            }
            sums[static_cast<size_t>(block)] += filtered * filtered;
            counts[static_cast<size_t>(block)] += 1;
        }
        ++total_frames;
    }
    int64_t total_count(int channel) const {
        int64_t total = 0;
        for (auto count : block_count[channel]) total += count;
        return total;
    }
    double total_sumsq(int channel) const {
        double total = 0.0;
        for (auto value : block_sumsq[channel]) total += value;
        return total;
    }
    double overall_db(int channel) const {
        int64_t count = total_count(channel);
        return count > 0 ? linear_to_db(sqrt(total_sumsq(channel) / count)) : -numeric_limits<double>::infinity();
    }
    size_t num_blocks() const { return block_sumsq[0].size(); }
    double block_db(int channel, size_t block) const {
        int64_t count = block_count[channel][block];
        return count > 0 ? linear_to_db(sqrt(block_sumsq[channel][block] / static_cast<double>(count))) : -numeric_limits<double>::infinity();
    }
};

void accumulate(Decoder& decoder, SideStats& stats, double lowpass_fc) {
    stats.init(decoder.info().channels, decoder.info().sample_rate, lowpass_fc);
    const int64_t kFrames = 4096;
    vector<float> samples(static_cast<size_t>(kFrames) * stats.channels);
    int64_t frames_read;
    while ((frames_read = decoder.read_float(samples.data(), kFrames)) > 0)
        for (int64_t frame = 0; frame < frames_read; ++frame) stats.add_frame(&samples[static_cast<size_t>(frame) * stats.channels]);
}

ContentCheckResult verify_mp3_content(const fs::path& out, const fs::path& source) {
    ContentCheckResult result;
    string error_message;
    auto source_decoder = Decoder::open(source, error_message);
    if (!source_decoder) {
        result.error = "audio content: cannot reopen source: " + error_message;
        return result;
    }
    auto output_decoder = Decoder::open(out, error_message);
    if (!output_decoder) {
        result.error = "audio content: cannot open output: " + error_message;
        return result;
    }

    const AudioInfo& source_info = source_decoder->info();
    const AudioInfo& output_info = output_decoder->info();
    if (output_info.channels != source_info.channels) {
        result.error = "audio content: channel count " + to_string(output_info.channels) + ", expected " + to_string(source_info.channels);
        return result;
    }

    // Task 18 fix round 2: one shared analysis cutoff for both sides, so a 32 kHz-class source
    // (or output) isn't left completely unfiltered while the other side gets the full 16 kHz
    // lowpass -- that asymmetry, not just a too-high fixed 16 kHz, was rejecting bright material
    // at low sample rates. 0.4x (not 0.45x) leaves the per-side Lowpass::design() Nyquist guard
    // as a pure safety net that a correctly computed shared cutoff should never actually hit.
    double lowpass_fc = std::min(16000.0, 0.4 * std::min(source_info.sample_rate, output_info.sample_rate));

    SideStats source_stats, mp3;
    accumulate(*source_decoder, source_stats, lowpass_fc);
    accumulate(*output_decoder, mp3, lowpass_fc);
    result.peak_dbfs = linear_to_db(mp3.peak);

    // Length: decoded frames must equal the frames actually read from the source (not the
    // source's declared frame count, which would misreport a truncated *source* as a bad output)
    // exactly at an unchanged rate, else land within +-2 frames of
    // round(source_frames_read * out_rate / in_rate).
    bool same_rate = output_info.sample_rate == source_info.sample_rate;
    int64_t expected = same_rate ? source_stats.total_frames
                                  : llround(static_cast<double>(source_stats.total_frames) * output_info.sample_rate / source_info.sample_rate);
    int64_t diff = std::llabs(mp3.total_frames - expected);
    if (same_rate ? (diff != 0) : (diff > 2)) {
        result.error = "audio content: length " + to_string(mp3.total_frames) + " frames, expected " + to_string(expected) +
                  (same_rate ? "" : " (±2)");
        return result;
    }

    // (a) per-channel overall RMS within +-0.5 dB; a near-silent source channel (below -60
    // dBFS, where a relative dB comparison is numerically unstable) just needs the decoded
    // channel to also be quiet (below -50 dBFS), not an exact match.
    for (int channel = 0; channel < source_info.channels; ++channel) {
        double src_db = source_stats.overall_db(channel), out_db = mp3.overall_db(channel);
        if (src_db < -60.0) {
            if (out_db >= -50.0) {
                result.error = "audio content: channel " + to_string(channel) + " decoded RMS " + db_str(out_db) +
                          " dBFS, expected below -50.00 dBFS (source is near-silent, " + db_str(src_db) + " dBFS)";
                return result;
            }
        } else if (fabs(out_db - src_db) > 0.5) {
            result.error = "audio content: channel " + to_string(channel) + " RMS " + db_str(out_db) + " dBFS, expected " + db_str(src_db) +
                      " dBFS (±0.5 dB)";
            return result;
        }
    }

    // (b) every aligned 1 s block whose source RMS is above -50 dBFS matches within +-1.0 dB;
    // the first and last block are skipped.
    size_t blocks = std::min(source_stats.num_blocks(), mp3.num_blocks());
    if (blocks >= 3) {
        for (size_t block = 1; block + 1 < blocks; ++block) {
            for (int channel = 0; channel < source_info.channels; ++channel) {
                double src_db = source_stats.block_db(channel, block);
                if (src_db <= -50.0) continue;
                double out_db = mp3.block_db(channel, block);
                if (fabs(out_db - src_db) > 1.0) {
                    result.error = "audio content: 1 s block " + to_string(block) + " channel " + to_string(channel) + " RMS " +
                              db_str(out_db) + " dBFS, expected " + db_str(src_db) + " dBFS (±1.0 dB)";
                    return result;
                }
            }
        }
    }
    return result;
}

}  // namespace

ContentCheckResult verify_content(const fs::path& out, const EncodeSettings& settings, const fs::path& source) {
    if (settings.format == Format::Flac) {
        ContentCheckResult result;
        result.error = verify_flac_content(out, source);
        return result;
    }
    return verify_mp3_content(out, source);
}

}  // namespace beatdown
