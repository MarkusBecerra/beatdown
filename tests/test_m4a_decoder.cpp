#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <type_traits>
#include <vector>
#include "core/decoder.hpp"
#include "core/m4a_decoder.hpp"
#include "core/unicode.hpp"
#include "fixtures.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

namespace {

// Everything `decoder` has left, interleaved.
template <typename Sample>
vector<Sample> read_all(Decoder& decoder) {
    const int channels = decoder.info().channels;
    vector<Sample> all, buffer(static_cast<size_t>(4096) * channels);
    for (;;) {
        int64_t frames_read;
        if constexpr (std::is_same_v<Sample, float>) frames_read = decoder.read_float(buffer.data(), 4096);
        else frames_read = decoder.read_int(buffer.data(), 4096);
        if (frames_read <= 0) break;
        all.insert(all.end(), buffer.begin(), buffer.begin() + frames_read * channels);
    }
    return all;
}

double rms_dbfs(const vector<float>& samples, size_t first, size_t last) {
    double sum = 0.0;
    for (size_t index = first; index < last; ++index) sum += double(samples[index]) * samples[index];
    return 20.0 * log10(sqrt(sum / double(last - first)));
}

struct PacketSpan { int64_t pos; int size; };

// Where each audio packet sits in `file`, found with FFmpeg's own demuxer.
vector<PacketSpan> audio_packets(const fs::path& file) {
    AVFormatContext* context = nullptr;
    REQUIRE(avformat_open_input(&context, path_to_utf8(file).c_str(), nullptr, nullptr) == 0);
    vector<PacketSpan> packets;
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(context, packet) >= 0) {
        packets.push_back({packet->pos, packet->size});
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&context);
    return packets;
}

// The file offset just past the last audio packet that ends before `fraction` of `file`. Cutting
// the file there leaves only whole packets, so nothing is damaged: the audio simply runs out
// before the index says it should.
int64_t packet_boundary(const fs::path& file, double fraction) {
    const auto target = static_cast<int64_t>(static_cast<double>(fs::file_size(file)) * fraction);
    int64_t boundary = 0;
    for (const PacketSpan& packet : audio_packets(file)) {
        if (packet.pos + packet.size > target) break;
        boundary = packet.pos + packet.size;
    }
    return boundary;
}

// Most-significant bit first, the order ALAC's bitstream uses.
struct BitWriter {
    string bytes;
    int used = 0;   // bits already used in bytes.back()
    void put(uint32_t value, int count) {
        for (int bit = count - 1; bit >= 0; --bit) {
            if (used == 0) bytes.push_back('\0');
            if ((value >> bit) & 1u) bytes.back() = static_cast<char>(bytes.back() | (0x80 >> used));
            used = (used + 1) % 8;
        }
    }
};

}  // namespace

TEST_CASE("looks_like_mp4 goes by the ftyp box, not the file name") {
    TempDir temp_dir;
    auto m4a = make_m4a(temp_dir.path / "a.m4a", M4aCodec::Aac, {.seconds = 0.1});
    REQUIRE(looks_like_mp4(m4a));
    fs::copy_file(m4a, temp_dir.path / "renamed.wav");
    REQUIRE(looks_like_mp4(temp_dir.path / "renamed.wav"));
    REQUIRE_FALSE(looks_like_mp4(make_audio(temp_dir.path / "a.wav", {.seconds = 0.1})));
    REQUIRE_FALSE(looks_like_mp4(make_audio(temp_dir.path / "a.aiff", {.container = SF_FORMAT_AIFF, .seconds = 0.1})));
    REQUIRE_FALSE(looks_like_mp4(make_audio(temp_dir.path / "a.flac", {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16, .seconds = 0.1})));
    // A RIFF length field that happens to spell "ftyp" is still a WAV: "RIFF" read as a box size is ~1.4 GB.
    write_bytes(temp_dir.path / "riff.wav", "RIFFftypWAVEfmt ");
    REQUIRE_FALSE(looks_like_mp4(temp_dir.path / "riff.wav"));
    write_bytes(temp_dir.path / "short.m4a", "ftyp");
    REQUIRE_FALSE(looks_like_mp4(temp_dir.path / "short.m4a"));
    REQUIRE_FALSE(looks_like_mp4(temp_dir.path / "missing.m4a"));
}

TEST_CASE("Decoder reads ALAC M4A bit-exactly at 16 and 24 bits") {
    const int subtype = GENERATE(Catch::Generators::as<int>{}, SF_FORMAT_PCM_16, SF_FORMAT_PCM_24);
    TempDir temp_dir;
    FixtureSpec spec{.subtype = subtype, .rate = 44100, .channels = 2, .seconds = 0.5};
    auto file = make_m4a(temp_dir.path / "a.m4a", M4aCodec::Alac, spec);
    string error_message;
    auto decoder = Decoder::open(file, error_message);
    INFO(error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->info().channels == 2);
    REQUIRE(decoder->info().sample_rate == 44100);
    REQUIRE(decoder->info().bits == (subtype == SF_FORMAT_PCM_16 ? 16 : 24));
    REQUIRE_FALSE(decoder->info().is_float);
    REQUIRE_FALSE(decoder->info().is_mpeg());
    REQUIRE(decoder->info().frames == 22050);
    REQUIRE(read_all<int32_t>(*decoder) == alac_samples(spec));
    REQUIRE(decoder->read_error().empty());
    REQUIRE(decoder->info().frames == 22050);
}

TEST_CASE("Decoder reads ALAC as float with libsndfile's scaling") {
    TempDir temp_dir;
    FixtureSpec spec{.subtype = SF_FORMAT_PCM_24, .seconds = 0.2};
    string error_message;
    auto decoder = Decoder::open(make_m4a(temp_dir.path / "a.m4a", M4aCodec::Alac, spec), error_message);
    REQUIRE(decoder);
    vector<float> samples = read_all<float>(*decoder);
    vector<int32_t> expected = alac_samples(spec);
    REQUIRE(samples.size() == expected.size());
    for (size_t index = 0; index < samples.size(); ++index)
        REQUIRE(samples[index] == static_cast<float>(expected[index] / 2147483648.0));
}

TEST_CASE("Decoder reads multichannel ALAC") {
    TempDir temp_dir;
    FixtureSpec spec{.subtype = SF_FORMAT_PCM_24, .rate = 48000, .channels = 6, .seconds = 0.3};
    string error_message;
    auto decoder = Decoder::open(make_m4a(temp_dir.path / "surround.m4a", M4aCodec::Alac, spec), error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->info().channels == 6);
    REQUIRE(read_all<int32_t>(*decoder) == alac_samples(spec));
}

TEST_CASE("Decoder reads AAC M4A at its level and length") {
    auto [rate, channels] = GENERATE(std::pair{44100, 2}, std::pair{48000, 1}, std::pair{32000, 2});
    TempDir temp_dir;
    FixtureSpec spec{.rate = rate, .channels = channels, .seconds = 2.0, .amplitude = 0.5};
    string error_message;
    auto decoder = Decoder::open(make_m4a(temp_dir.path / "a.m4a", M4aCodec::Aac, spec), error_message);
    INFO(error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->info().sample_rate == rate);
    REQUIRE(decoder->info().channels == channels);
    REQUIRE(decoder->info().is_float);
    REQUIRE(decoder->info().bits == 32);
    REQUIRE(decoder->info().frames == 2 * rate);

    vector<float> samples = read_all<float>(*decoder);
    REQUIRE(decoder->read_error().empty());
    // Gapless: the encoder's priming and padding are both gone, so the length is the source's...
    REQUIRE(static_cast<int64_t>(samples.size() / channels) == 2 * rate);
    REQUIRE(decoder->info().frames == 2 * rate);
    // ...and so is the timing: the decode lines up with the original sine sample for sample
    // (a priming delay left in or trimmed twice would shift it by ~23 ms and leave a residual
    // as loud as the signal). A -6 dBFS-peak sine is -9.03 dBFS RMS.
    vector<float> original = sine_for_test(FixtureSpec{.rate = rate, .channels = channels, .seconds = 2.0, .amplitude = 0.5});
    vector<float> residual(samples.size());
    for (size_t index = 0; index < samples.size(); ++index) residual[index] = samples[index] - original[index];
    const size_t middle_first = static_cast<size_t>(rate / 2) * channels, middle_last = static_cast<size_t>(rate * 3 / 2) * channels;
    REQUIRE(rms_dbfs(samples, middle_first, middle_last) == Catch::Approx(20.0 * log10(0.5 / sqrt(2.0))).margin(0.25));
    REQUIRE(rms_dbfs(residual, middle_first, middle_last) < -40.0);
}

TEST_CASE("Decoder exposes M4A tags") {
    TempDir temp_dir;
    Tags tags;
    tags.title = "Deep Cut"; tags.artist = "Måns – 東京"; tags.album = "Sept Drop"; tags.date = "2024";
    tags.track = "3/12"; tags.genre = "House"; tags.comment = "promo";
    string error_message;
    auto decoder = Decoder::open(make_m4a(temp_dir.path / "tagged.m4a", M4aCodec::Aac, {.seconds = 0.2, .tags = tags}), error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->tags().title == "Deep Cut");
    REQUIRE(decoder->tags().artist == "Måns – 東京");
    REQUIRE(decoder->tags().album == "Sept Drop");
    REQUIRE(decoder->tags().date == "2024");
    REQUIRE(decoder->tags().track == "3/12");
    REQUIRE(decoder->tags().genre == "House");
    REQUIRE(decoder->tags().comment == "promo");

    auto untagged = Decoder::open(make_m4a(temp_dir.path / "plain.m4a", M4aCodec::Alac, {.seconds = 0.2}), error_message);
    REQUIRE(untagged);
    REQUIRE(untagged->tags().empty());
}

TEST_CASE("Decoder opens M4A with the index in front, at a non-ASCII path, or under another extension") {
    TempDir temp_dir;
    FixtureSpec spec{.subtype = SF_FORMAT_PCM_16, .seconds = 0.3};
    string error_message;
    auto moov_first = Decoder::open(make_m4a(temp_dir.path / "front.m4a", M4aCodec::Alac, spec, true), error_message);
    REQUIRE(moov_first);
    REQUIRE(read_all<int32_t>(*moov_first) == alac_samples(spec));

    auto unicode = Decoder::open(make_m4a(temp_dir.path / path_from_utf8("Måns – 東京.m4a"), M4aCodec::Alac, spec), error_message);
    REQUIRE(unicode);
    REQUIRE(read_all<int32_t>(*unicode) == alac_samples(spec));

    // Recognised by content, like an MP3 renamed to .wav.
    fs::copy_file(temp_dir.path / "front.m4a", temp_dir.path / "misnamed.wav");
    auto misnamed = Decoder::open(temp_dir.path / "misnamed.wav", error_message);
    REQUIRE(misnamed);
    REQUIRE(read_all<int32_t>(*misnamed) == alac_samples(spec));
}

TEST_CASE("Decoder refuses an MP4 whose audio isn't AAC or ALAC") {
    TempDir temp_dir;
    string error_message;
    auto decoder = Decoder::open(make_m4a(temp_dir.path / "ac3.m4a", M4aCodec::Ac3, {.rate = 48000, .seconds = 0.2}), error_message);
    REQUIRE_FALSE(decoder);
    REQUIRE_THAT(error_message, ContainsSubstring("unsupported audio codec"));
    REQUIRE_THAT(error_message, ContainsSubstring("ac3"));
}

TEST_CASE("Decoder fails a damaged M4A with a message, not a crash") {
    TempDir temp_dir;
    string error_message;
    // A valid 20-byte ftyp box, then garbage. Sized by sizeof, not by hand: it embeds NULs (see kCorruptWav).
    static constexpr char kJunkM4a[] = "\0\0\0\x14" "ftypM4A \0\0\0\0M4A " "junkjunkjunkjunk";
    write_bytes(temp_dir.path / "junk.m4a", string_view(kJunkM4a, sizeof kJunkM4a - 1));
    REQUIRE_FALSE(Decoder::open(temp_dir.path / "junk.m4a", error_message));
    REQUIRE_FALSE(error_message.empty());

    // FFmpeg's muxer writes the index last, so cutting the file short loses the index itself.
    auto file = make_m4a(temp_dir.path / "cut.m4a", M4aCodec::Aac, {.seconds = 1.0});
    fs::resize_file(file, fs::file_size(file) / 2);
    error_message.clear();
    REQUIRE_FALSE(Decoder::open(file, error_message));
    REQUIRE_FALSE(error_message.empty());
}

// Verification re-decodes the source through the same decoder, so a file that's missing audio
// would match itself; the decoder has to notice on its own, and say so rather than just stop.
TEST_CASE("Decoder reports an M4A whose audio ends early instead of just stopping") {
    const M4aCodec codec = GENERATE(M4aCodec::Aac, M4aCodec::Alac);
    const bool on_packet_boundary = GENERATE(true, false);
    TempDir temp_dir;
    // Index in front, as iTunes writes it: the index survives the cut and still promises 4 s.
    auto file = make_m4a(temp_dir.path / "cut.m4a", codec, {.subtype = SF_FORMAT_PCM_16, .rate = 44100, .seconds = 4.0}, true);
    // Cut between packets, the demuxer just runs out (only the length check can tell); cut
    // mid-packet, the demuxer flags the packet it could only half read.
    fs::resize_file(file, on_packet_boundary ? packet_boundary(file, 0.5) : fs::file_size(file) / 2 + 7);
    string error_message;
    auto decoder = Decoder::open(file, error_message);
    INFO(error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->info().frames == 4 * 44100);
    vector<float> samples = read_all<float>(*decoder);
    INFO(decoder->read_error());
    REQUIRE(samples.size() / 2 < 3 * 44100);
    REQUIRE_THAT(decoder->read_error(), ContainsSubstring(on_packet_boundary ? "the audio ends early" : "an audio packet is cut short"));
    REQUIRE(decoder->read_float(samples.data(), 1) == 0);   // stays stopped
}

TEST_CASE("M4A seek_start rewinds to the first frame") {
    TempDir temp_dir;
    FixtureSpec spec{.subtype = SF_FORMAT_PCM_16, .seconds = 0.5};
    string error_message;
    auto decoder = Decoder::open(make_m4a(temp_dir.path / "a.m4a", M4aCodec::Alac, spec), error_message);
    REQUIRE(decoder);
    vector<int32_t> buffer(1000 * 2);
    REQUIRE(decoder->read_int(buffer.data(), 1000) == 1000);
    REQUIRE(decoder->seek_start());
    REQUIRE(read_all<int32_t>(*decoder) == alac_samples(spec));
    REQUIRE(decoder->seek_start());
    REQUIRE(read_all<int32_t>(*decoder) == alac_samples(spec));
}

TEST_CASE("M4A seek_start that can't reopen the file leaves a decoder that reads nothing and says why") {
    TempDir temp_dir;
    auto file = make_m4a(temp_dir.path / "a.m4a", M4aCodec::Alac, {.subtype = SF_FORMAT_PCM_16, .seconds = 0.2});
    string error_message;
    auto decoder = Decoder::open(file, error_message);
    REQUIRE(decoder);
    error_code remove_error;
    fs::remove(file, remove_error);   // Windows refuses to delete a file that's open
    if (remove_error) SKIP("cannot remove an open file here: " + remove_error.message());
    REQUIRE_FALSE(decoder->seek_start());
    REQUIRE_FALSE(decoder->read_error().empty());
    vector<float> buffer(1024 * 2);
    REQUIRE(decoder->read_float(buffer.data(), 1024) == 0);
    vector<int32_t> ints(1024 * 2);
    REQUIRE(decoder->read_int(ints.data(), 1024) == 0);
}

// ALAC and AAC in an MP4 carry no checksum, so damage can decode without the decoder noticing.
// One thing it can still get wrong is the length: this packet is valid ALAC (an uncompressed block
// of silence), but holds far fewer samples than the container gave it -- what 13% of
// garbage-filled ALAC packets looked like, measured. Only comparing the two catches it.
TEST_CASE("Decoder rejects an M4A packet that decodes to a different length than the file gives it") {
    TempDir temp_dir;
    auto file = make_m4a(temp_dir.path / "a.m4a", M4aCodec::Alac, {.subtype = SF_FORMAT_PCM_16, .rate = 44100, .channels = 1, .seconds = 2.0});
    vector<PacketSpan> packets = audio_packets(file);
    REQUIRE(packets.size() > 3);
    // An uncompressed mono element of N 16-bit samples plus its end tag takes 58 + 16N bits, and
    // ALAC allows at most 8 unused bits at the end: an even-sized packet leaves 6.
    auto victim = find_if(packets.begin() + 1, packets.end() - 1, [](const PacketSpan& packet) { return packet.size % 2 == 0; });
    REQUIRE(victim != packets.end() - 1);
    const auto samples = static_cast<uint32_t>((victim->size * 8 - 58) / 16);
    REQUIRE(samples > 0);
    REQUIRE(samples < 4096);   // the container gives every packet but the last 4096
    BitWriter bits;
    bits.put(0, 3);         // SCE: a single-channel element
    bits.put(0, 4 + 12);    // element instance tag, unused header bits
    bits.put(1, 1);         // has_size: the sample count follows
    bits.put(0, 2);         // no extra low bits
    bits.put(1, 1);         // not compressed: raw samples follow
    bits.put(samples, 32);
    for (uint32_t index = 0; index < samples; ++index) bits.put(0, 16);
    bits.put(7, 3);         // END
    REQUIRE(bits.bytes.size() == static_cast<size_t>(victim->size));
    string data = read_file(file);
    data.replace(static_cast<size_t>(victim->pos), bits.bytes.size(), bits.bytes);
    write_bytes(file, data);

    string error_message;
    auto decoder = Decoder::open(file, error_message);
    INFO(error_message);
    REQUIRE(decoder);
    read_all<float>(*decoder);
    REQUIRE_THAT(decoder->read_error(), ContainsSubstring("a packet decoded to " + to_string(samples) + " samples where the file says 4096"));
}
