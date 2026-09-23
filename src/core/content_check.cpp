#include "core/content_check.hpp"
#include <lame/lame.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <limits>
#include <vector>
#include "core/decoder.hpp"
#include "core/mp3_parse.hpp"

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
        // resulting ~1-LSB-at-24-bit rounding.
        const double kTolerance = 2.0 / 8388608.0;
        std::vector<float> sbuf(static_cast<size_t>(kFrames) * channels), obuf(static_cast<size_t>(kFrames) * channels);
        for (;;) {
            int64_t sn = so->read_float(sbuf.data(), kFrames);
            int64_t on = od->read_float(obuf.data(), kFrames);
            int64_t n = std::min(sn, on);
            for (int64_t i = 0; i < n; ++i) {
                for (int c = 0; c < channels; ++c) {
                    double a = sbuf[static_cast<size_t>(i) * channels + c], b = obuf[static_cast<size_t>(i) * channels + c];
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

// --- MP3: RMS-based level comparison, decoded with LAME's public "hip" decoder API ---

// Accumulates per-channel sum-of-squares, overall and per aligned 1-second block, without
// holding the decoded/source audio itself in memory (only one double+count per block).
struct SideStats {
    int channels = 0;
    int rate = 0;
    std::vector<double> block_sumsq[2];
    std::vector<int64_t> block_count[2];

    void add(int c, int64_t block, double v) {
        if (block < 0) return;
        auto& sums = block_sumsq[c];
        auto& counts = block_count[c];
        if (static_cast<size_t>(block) >= sums.size()) {
            sums.resize(static_cast<size_t>(block) + 1, 0.0);
            counts.resize(static_cast<size_t>(block) + 1, 0);
        }
        sums[static_cast<size_t>(block)] += v * v;
        counts[static_cast<size_t>(block)] += 1;
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

void accumulate_source(Decoder& d, SideStats& st) {
    const AudioInfo& info = d.info();
    st.channels = info.channels;
    st.rate = info.sample_rate;
    const int64_t kFrames = 4096;
    std::vector<float> buf(static_cast<size_t>(kFrames) * st.channels);
    int64_t n, pos = 0;
    while ((n = d.read_float(buf.data(), kFrames)) > 0) {
        for (int64_t i = 0; i < n; ++i) {
            int64_t block = (pos + i) / st.rate;
            for (int c = 0; c < st.channels; ++c) st.add(c, block, buf[static_cast<size_t>(i) * st.channels + c]);
        }
        pos += n;
    }
}

// Decodes `out` in full with LAME's public hip decoder (never the unclipped/unpublished
// variant), skipping the leading ID3v2 tag, and accumulates it the same way as the source.
// The decoded stream starts (enc_delay + 529) samples late at the MP3's own rate (spike
// measurement, constant, no drift) — those priming samples are excluded from every block. The
// last enc_padding samples are the encoder's own trailing pad (silence, added to fill out the
// last MPEG frame): held back in a small FIFO and dropped at EOF, rather than committed as if
// they were real signal, so it doesn't quietly pull down the RMS of a short clip.
std::string accumulate_mp3(const fs::path& out, const Mp3Info& info, SideStats& st) {
    st.channels = info.channels;
    st.rate = info.sample_rate;

    std::ifstream in(out, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t q = info.id3v2_size;

    hip_t h = hip_decode_init();
    if (!h) return "audio content: hip_decode_init failed";
    struct HipGuard {
        hip_t h;
        ~HipGuard() { hip_decode_exit(h); }
    } guard{h};

    int enc_delay = -1, enc_padding = -1;
    short pl[4608], pr[4608];
    unsigned char dummy[4] = {0, 0, 0, 0};
    mp3data_struct md{};
    int64_t global = 0;
    int64_t committed = 0;         // frames actually handed to the block accumulators so far
    std::deque<double> pending[2]; // per-channel FIFO of decoded-but-not-yet-committed samples
    const size_t kChunk = 4096;
    // "returns at most one frame" per call: mpglib can hold more than one already-fed frame
    // buffered internally, only emitting one at a time, so once the whole file has been fed
    // (q == data.size()) a run of 0-length "priming" calls can still keep producing output for a
    // while. Only treat it as fully drained after several such calls in a row come back empty --
    // a short file needs more of these than a long one, but the number of frames it can have
    // buffered is small and bounded, so this always terminates.
    int empty_primes_at_eof = 0;
    for (;;) {
        int ret = hip_decode1_headersB(h, dummy, 0, pl, pr, &md, &enc_delay, &enc_padding);
        while (ret == 0 && q < data.size()) {
            size_t len = std::min(kChunk, data.size() - q);
            ret = hip_decode1_headersB(h, &data[q], len, pl, pr, &md, &enc_delay, &enc_padding);
            q += len;
        }
        if (ret < 0) return "audio content: MP3 decode error " + std::to_string(ret);
        if (ret == 0) {
            if (q < data.size()) break;   // defensive: the inner loop above always drains q first
            if (++empty_primes_at_eof > 16) break;
            continue;
        }
        empty_primes_at_eof = 0;
        // enc_delay is -1 until the LAME Info/Xing tag frame (always the first frame beatdown
        // writes) is parsed; every real output has it, but 576 (LAME's fixed encoder algorithmic
        // delay) is a safe fallback rather than misaligning on some unexpected stream. Padding is
        // similarly latched once known (0 until then, which just means "commit immediately").
        int64_t delay = (enc_delay >= 0 ? enc_delay : 576) + 529;
        int64_t padding = enc_padding > 0 ? enc_padding : 0;
        for (int i = 0; i < ret; ++i, ++global) {
            if (global < delay) continue;
            pending[0].push_back(pl[i] / 32768.0);
            if (st.channels == 2) pending[1].push_back(pr[i] / 32768.0);
            if (static_cast<int64_t>(pending[0].size()) > padding) {
                int64_t block = committed / st.rate;
                st.add(0, block, pending[0].front());
                pending[0].pop_front();
                if (st.channels == 2) {
                    st.add(1, block, pending[1].front());
                    pending[1].pop_front();
                }
                ++committed;
            }
        }
    }
    return "";
}

std::string verify_mp3_content(const fs::path& out, const fs::path& source) {
    std::string err;
    auto sd = Decoder::open(source, err);
    if (!sd) return "audio content: cannot reopen source: " + err;
    const AudioInfo& si = sd->info();

    Mp3Info info;
    if (!parse_mp3(out, info, err)) return "audio content: cannot parse output MP3: " + err;
    if (info.channels != si.channels)
        return "audio content: channel count " + std::to_string(info.channels) + ", expected " + std::to_string(si.channels);

    SideStats src;
    accumulate_source(*sd, src);
    SideStats mp3;
    std::string derr = accumulate_mp3(out, info, mp3);
    if (!derr.empty()) return derr;

    // (a) per-channel overall RMS within ±0.5 dB; a near-silent source channel (below -60
    // dBFS, where a relative dB comparison is numerically unstable) just needs the decoded
    // channel to also be quiet (below -50 dBFS), not an exact match.
    for (int c = 0; c < si.channels; ++c) {
        double src_db = src.overall_db(c), out_db = mp3.overall_db(c);
        if (src_db < -60.0) {
            if (out_db >= -50.0)
                return "audio content: channel " + std::to_string(c) + " decoded RMS " + db_str(out_db) +
                       " dBFS, expected below -50.00 dBFS (source is near-silent, " + db_str(src_db) + " dBFS)";
        } else if (std::fabs(out_db - src_db) > 0.5) {
            return "audio content: channel " + std::to_string(c) + " RMS " + db_str(out_db) + " dBFS, expected " + db_str(src_db) +
                   " dBFS (±0.5 dB)";
        }
    }

    // (b) every aligned 1 s block whose source RMS is above -50 dBFS matches within ±1.0 dB;
    // the first and last block (encoder priming/padding edge effects) are skipped.
    size_t blocks = std::min(src.num_blocks(), mp3.num_blocks());
    if (blocks >= 3) {
        for (size_t k = 1; k + 1 < blocks; ++k) {
            for (int c = 0; c < si.channels; ++c) {
                double src_db = src.block_db(c, k);
                if (src_db <= -50.0) continue;
                double out_db = mp3.block_db(c, k);
                if (std::fabs(out_db - src_db) > 1.0)
                    return "audio content: 1 s block " + std::to_string(k) + " channel " + std::to_string(c) + " RMS " +
                           db_str(out_db) + " dBFS, expected " + db_str(src_db) + " dBFS (±1.0 dB)";
            }
        }
    }
    return "";
}

}  // namespace

std::string verify_content(const fs::path& out, const EncodeSettings& s, const fs::path& source) {
    return s.format == Format::Flac ? verify_flac_content(out, source) : verify_mp3_content(out, source);
}

}  // namespace beatdown
