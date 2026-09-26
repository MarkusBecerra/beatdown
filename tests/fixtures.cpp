#include "fixtures.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <stdexcept>
#include <vector>
#include "core/platform/platform.hpp"
#include "core/unicode.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

// Global scope, like fixtures.hpp -- see its comment for why these aren't routed through
// core/std_names.hpp.
using std::error_code;
using std::ifstream;
using std::ios;
using std::istreambuf_iterator;
using std::llround;
using std::mt19937_64;
using std::ofstream;
using std::optional;
using std::random_device;
using std::runtime_error;
using std::sin;
using std::streamsize;
using std::to_string;
using std::unique_ptr;

namespace {
constexpr double kPi = 3.14159265358979323846;

// `spec`'s test sine, interleaved, the same value on every channel.
vector<float> sine(const FixtureSpec& spec) {
    const int64_t frames = static_cast<int64_t>(spec.rate * spec.seconds);
    vector<float> samples(static_cast<size_t>(frames) * spec.channels);
    for (int64_t frame = 0; frame < frames; ++frame) {
        float sample = static_cast<float>(spec.amplitude * sin(2.0 * kPi * spec.freq_hz * frame / spec.rate));
        for (int channel = 0; channel < spec.channels; ++channel) samples[frame * spec.channels + channel] = sample;
    }
    return samples;
}

int alac_bits(const FixtureSpec& spec) { return spec.subtype == SF_FORMAT_PCM_16 ? 16 : 24; }

string av_error_text(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

void check(int result, const string& what) {
    if (result < 0) throw runtime_error("make_m4a: " + what + ": " + av_error_text(result));
}
}  // namespace

TempDir::TempDir() {
    mt19937_64 rng(random_device{}());
    path = fs::temp_directory_path() / ("beatdown-test-" + to_string(rng()));
    fs::create_directories(path);
}
TempDir::~TempDir() {
    error_code ignored_error;
    fs::remove_all(path, ignored_error);
}

fs::path make_audio(const fs::path& file, const FixtureSpec& spec) {
    fs::create_directories(file.parent_path());
    SF_INFO info{};
    info.samplerate = spec.rate;
    info.channels = spec.channels;
    info.format = spec.container | spec.subtype;
    SNDFILE* sndfile = beatdown::platform::sf_open_path(file, SFM_WRITE, &info);
    if (!sndfile) throw runtime_error("fixture open failed: " + beatdown::path_to_utf8(file) + ": " + sf_strerror(nullptr));
    auto set = [&](int key, const optional<string>& value) { if (value) sf_set_string(sndfile, key, value->c_str()); };
    set(SF_STR_TITLE, spec.tags.title);
    set(SF_STR_ARTIST, spec.tags.artist);
    set(SF_STR_ALBUM, spec.tags.album);
    set(SF_STR_DATE, spec.tags.date);
    set(SF_STR_TRACKNUMBER, spec.tags.track);
    set(SF_STR_GENRE, spec.tags.genre);
    set(SF_STR_COMMENT, spec.tags.comment);

    vector<float> samples = sine(spec);
    sf_writef_float(sndfile, samples.data(), static_cast<sf_count_t>(samples.size() / spec.channels));
    sf_close(sndfile);
    return file;
}

void write_bytes(const fs::path& file, string_view bytes) {
    fs::create_directories(file.parent_path());
    ofstream out(file, ios::binary);
    out.write(bytes.data(), static_cast<streamsize>(bytes.size()));
}

string read_file(const fs::path& file) {
    ifstream in(file, ios::binary);
    return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

vector<float> sine_for_test(const FixtureSpec& spec) { return sine(spec); }

vector<int32_t> alac_samples(const FixtureSpec& spec) {
    const int bits = alac_bits(spec);
    const double full_scale = double((1 << (bits - 1)) - 1);
    vector<int32_t> samples;
    for (float sample : sine(spec)) samples.push_back(static_cast<int32_t>(llround(sample * full_scale)) * (1 << (32 - bits)));
    return samples;
}

fs::path make_m4a(const fs::path& file, M4aCodec codec, const FixtureSpec& spec, bool moov_first) {
    av_log_set_level(AV_LOG_QUIET);
    fs::create_directories(file.parent_path());
    const string utf8_path = beatdown::path_to_utf8(file);
    const char* encoder_name = codec == M4aCodec::Aac ? "aac" : codec == M4aCodec::Alac ? "alac" : "ac3";
    const AVCodec* encoder = avcodec_find_encoder_by_name(encoder_name);
    if (!encoder) throw runtime_error(string("make_m4a: FFmpeg has no ") + encoder_name + " encoder");

    AVFormatContext* raw_output = nullptr;
    check(avformat_alloc_output_context2(&raw_output, nullptr, codec == M4aCodec::Ac3 ? "mp4" : "ipod", utf8_path.c_str()), "output context");
    auto close_output = [](AVFormatContext* context) { if (context->pb) avio_closep(&context->pb); avformat_free_context(context); };
    unique_ptr<AVFormatContext, decltype(close_output)> output(raw_output, close_output);
    auto free_codec = [](AVCodecContext* context) { avcodec_free_context(&context); };
    unique_ptr<AVCodecContext, decltype(free_codec)> context(avcodec_alloc_context3(encoder), free_codec);

    const bool alac = codec == M4aCodec::Alac, alac16 = alac && alac_bits(spec) == 16;
    context->sample_rate = spec.rate;
    av_channel_layout_default(&context->ch_layout, spec.channels);
    context->sample_fmt = alac ? (alac16 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P) : AV_SAMPLE_FMT_FLTP;
    if (alac && !alac16) context->bits_per_raw_sample = 24;
    if (!alac) context->bit_rate = 128000 * spec.channels;
    context->time_base = AVRational{1, spec.rate};
    if (output->oformat->flags & AVFMT_GLOBALHEADER) context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(context.get(), encoder, nullptr), string("open ") + encoder_name + " encoder");

    AVStream* stream = avformat_new_stream(output.get(), nullptr);
    if (!stream) throw runtime_error("make_m4a: new stream");
    check(avcodec_parameters_from_context(stream->codecpar, context.get()), "stream parameters");
    stream->time_base = context->time_base;
    auto tag = [&](const char* key, const optional<string>& value) { if (value) av_dict_set(&output->metadata, key, value->c_str(), 0); };
    tag("title", spec.tags.title);
    tag("artist", spec.tags.artist);
    tag("album", spec.tags.album);
    tag("date", spec.tags.date);
    tag("track", spec.tags.track);
    tag("genre", spec.tags.genre);
    tag("comment", spec.tags.comment);

    check(avio_open(&output->pb, utf8_path.c_str(), AVIO_FLAG_WRITE), "open " + utf8_path);
    AVDictionary* options = nullptr;
    if (moov_first) av_dict_set(&options, "movflags", "+faststart", 0);
    int header_result = avformat_write_header(output.get(), &options);
    av_dict_free(&options);
    check(header_result, "write header");

    auto free_packet = [](AVPacket* packet) { av_packet_free(&packet); };
    unique_ptr<AVPacket, decltype(free_packet)> packet(av_packet_alloc(), free_packet);
    auto drain = [&] {
        for (;;) {
            int result = avcodec_receive_packet(context.get(), packet.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
            check(result, "encode");
            av_packet_rescale_ts(packet.get(), context->time_base, stream->time_base);
            packet->stream_index = stream->index;
            check(av_interleaved_write_frame(output.get(), packet.get()), "write packet");
        }
    };

    const vector<float> floats = sine(spec);
    const vector<int32_t> ints = alac ? alac_samples(spec) : vector<int32_t>{};
    const int64_t frames = static_cast<int64_t>(floats.size() / spec.channels);
    const int frame_size = context->frame_size > 0 ? context->frame_size : 4096;
    // An encoder that can't take a short last frame (AC-3) gets the remainder padded with silence.
    const bool short_last = encoder->capabilities & (AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
    auto free_frame = [](AVFrame* frame) { av_frame_free(&frame); };
    unique_ptr<AVFrame, decltype(free_frame)> frame(av_frame_alloc(), free_frame);
    for (int64_t first = 0; first < frames; first += frame_size) {
        const int count = static_cast<int>(std::min<int64_t>(frame_size, frames - first));
        av_frame_unref(frame.get());
        frame->nb_samples = short_last ? count : frame_size;
        frame->format = context->sample_fmt;
        frame->sample_rate = spec.rate;
        check(av_channel_layout_copy(&frame->ch_layout, &context->ch_layout), "frame layout");
        check(av_frame_get_buffer(frame.get(), 0), "frame buffer");
        for (int channel = 0; channel < spec.channels; ++channel) {
            for (int index = 0; index < frame->nb_samples; ++index) {
                const bool real = index < count;   // false only in AC-3's padding
                const size_t at = static_cast<size_t>(first + index) * spec.channels + channel;
                if (!alac) reinterpret_cast<float*>(frame->extended_data[channel])[index] = real ? floats[at] : 0.0f;
                else if (alac16) reinterpret_cast<int16_t*>(frame->extended_data[channel])[index] = real ? static_cast<int16_t>(ints[at] >> 16) : int16_t(0);
                else reinterpret_cast<int32_t*>(frame->extended_data[channel])[index] = real ? ints[at] : 0;
            }
        }
        frame->pts = first;
        check(avcodec_send_frame(context.get(), frame.get()), "send frame");
        drain();
    }
    check(avcodec_send_frame(context.get(), nullptr), "flush");
    drain();
    check(av_write_trailer(output.get()), "write trailer");
    return file;
}
