#include "core/m4a_decoder.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include "core/std_names.hpp"
#include "core/unicode.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

namespace beatdown {

bool looks_like_mp4(const fs::path& path) {
    ifstream file(path, ios::binary);
    unsigned char header[8];
    if (!file.read(reinterpret_cast<char*>(header), sizeof header)) return false;
    // An MP4 file is a sequence of boxes -- a big-endian 32-bit size, then a four-character type
    // -- and a conforming one starts with 'ftyp'. The size check stops another format whose bytes
    // 4..7 merely happen to spell "ftyp" (a WAV's RIFF length field could, in theory) from
    // matching: an ftyp box is a few dozen bytes, while "RIFF", "FORM", "fLaC" or "ID3" read as a
    // box size is over a gigabyte.
    uint32_t size = (uint32_t(header[0]) << 24) | (uint32_t(header[1]) << 16) | (uint32_t(header[2]) << 8) | uint32_t(header[3]);
    return size >= 8 && size <= 4096 && memcmp(header + 4, "ftyp", 4) == 0;
}

namespace {

string av_error_text(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

// FFmpeg would otherwise print its own warnings to stderr, in the middle of the per-file report;
// every failure that matters comes back as an error code and is reported per file instead.
void silence_ffmpeg_log() {
    static const bool silenced = [] { av_log_set_level(AV_LOG_QUIET); return true; }();
    (void)silenced;
}

// Each FFmpeg object is owned by a unique_ptr whose deleter is FFmpeg's matching free function, so
// every exit path -- a failed open halfway through included -- releases exactly what was made.
struct FormatCloser { void operator()(AVFormatContext* context) const { avformat_close_input(&context); } };
struct CodecFreer { void operator()(AVCodecContext* context) const { avcodec_free_context(&context); } };
struct PacketFreer { void operator()(AVPacket* packet) const { av_packet_free(&packet); } };
struct FrameFreer { void operator()(AVFrame* frame) const { av_frame_free(&frame); } };
struct IoFreer {
    void operator()(AVIOContext* io) const {
        av_freep(&io->buffer);   // FFmpeg may have swapped in a buffer of its own; this frees whichever it holds
        avio_context_free(&io);
    }
};

// One decoded sample in the caller's representation, with libsndfile's scaling (see
// Decoder::read_float): float as is, integers left-justified in 32 bits, and each converted to the
// other by the full-scale factor for its width.
template <typename Out, typename In>
Out convert_sample(In sample) {
    if constexpr (std::is_same_v<In, float>) {
        if constexpr (std::is_same_v<Out, float>) return sample;
        else return static_cast<int32_t>(llround(std::clamp(static_cast<double>(sample), -1.0, 2147483647.0 / 2147483648.0) * 2147483648.0));
    } else if constexpr (std::is_same_v<In, int16_t>) {
        if constexpr (std::is_same_v<Out, float>) return static_cast<float>(sample / 32768.0);
        else return static_cast<int32_t>(sample) * 65536;
    } else {
        if constexpr (std::is_same_v<Out, float>) return static_cast<float>(sample / 2147483648.0);
        else return sample;
    }
}

// Copies `count` frames, starting at frame `first` of `frame` (planar or interleaved samples of
// type In), to `out` as interleaved Out.
template <typename In, typename Out>
void copy_as(const AVFrame& frame, int first, int count, int channels, Out* out) {
    const bool planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame.format));
    const size_t stride = planar ? 1 : static_cast<size_t>(channels);
    for (int channel = 0; channel < channels; ++channel) {
        const In* source = reinterpret_cast<const In*>(frame.extended_data[planar ? channel : 0]);
        source += planar ? static_cast<size_t>(first) : static_cast<size_t>(first) * channels + channel;
        for (int index = 0; index < count; ++index)
            out[static_cast<size_t>(index) * channels + channel] = convert_sample<Out>(source[static_cast<size_t>(index) * stride]);
    }
}

template <typename Out>
void copy_samples(const AVFrame& frame, int first, int count, int channels, Out* out) {
    switch (av_get_packed_sample_fmt(static_cast<AVSampleFormat>(frame.format))) {
        case AV_SAMPLE_FMT_FLT: copy_as<float>(frame, first, count, channels, out); break;
        case AV_SAMPLE_FMT_S16: copy_as<int16_t>(frame, first, count, channels, out); break;
        default: copy_as<int32_t>(frame, first, count, channels, out); break;   // S32, the only other one open() accepts
    }
}

optional<string> metadata_string(const AVDictionary* metadata, const char* key) {
    const AVDictionaryEntry* entry = av_dict_get(metadata, key, nullptr, 0);
    if (!entry || !entry->value || !*entry->value) return nullopt;
    return sanitize_utf8(entry->value);
}

// A decoded stream this much shorter than the container says it is has lost audio -- a truncated
// download, a damaged file -- rather than just the encoder's priming and padding, which trimming
// may or may not have removed and which never comes near this.
constexpr double kShortfallToleranceSeconds = 0.5;

class M4aDecoder : public Decoder {
public:
    explicit M4aDecoder(const fs::path& path) { path_ = path; }

    // Sets up everything from the file's first byte to its first decoded frame; open_m4a() and
    // seek_start() both go through here, so a rewind is exactly a fresh open.
    bool start(string& error);

    int64_t read_float(float* out, int64_t frames) override { return read(out, frames); }
    int64_t read_int(int32_t* out, int64_t frames) override { return read(out, frames); }
    bool seek_start() override {
        string error;
        if (start(error)) return true;
        read_error_ = error;
        return false;
    }

private:
    template <typename Sample> int64_t read(Sample* out, int64_t frames);
    bool next_frame();
    bool fail(const string& what, int av_error);
    void close();

    // FFmpeg reads the file through these rather than opening it itself: fs::path handles
    // non-ASCII names the same way as every other file beatdown opens, and FFmpeg's own
    // protocol layer -- network protocols included -- is never involved.
    static int read_packet(void* opaque, uint8_t* buffer, int size);
    static int64_t seek(void* opaque, int64_t offset, int whence);

    // Declaration order is teardown order, reversed: each object here is used by the ones
    // declared after it (the format context reads through io_, which reads file_), so each is
    // destroyed only after everything that depends on it.
    ifstream file_;
    int64_t file_size_ = 0;
    unique_ptr<AVIOContext, IoFreer> io_;
    unique_ptr<AVFormatContext, FormatCloser> format_;
    unique_ptr<AVCodecContext, CodecFreer> codec_;
    unique_ptr<AVPacket, PacketFreer> packet_;
    unique_ptr<AVFrame, FrameFreer> frame_;

    int stream_index_ = -1;
    AVSampleFormat sample_format_ = AV_SAMPLE_FMT_NONE;
    int frame_offset_ = 0;          // frames of frame_ already handed out
    int64_t decoded_frames_ = 0;    // every frame the decoder has produced so far
    int64_t delivered_frames_ = 0;  // every frame read() has handed out so far
    int64_t declared_frames_ = 0;   // the track's own length, edit list applied; 0 if it doesn't say
    string length_mismatch_;        // the latest frame's length disagreed with the container (see next_frame())
    bool mismatch_was_short_ = false;
    bool at_end_ = false;
};

int M4aDecoder::read_packet(void* opaque, uint8_t* buffer, int size) {
    ifstream& file = static_cast<M4aDecoder*>(opaque)->file_;
    file.read(reinterpret_cast<char*>(buffer), size);
    if (file.gcount() > 0) return static_cast<int>(file.gcount());
    return file.bad() ? AVERROR(EIO) : AVERROR_EOF;
}

int64_t M4aDecoder::seek(void* opaque, int64_t offset, int whence) {
    auto* decoder = static_cast<M4aDecoder*>(opaque);
    if (whence & AVSEEK_SIZE) return decoder->file_size_;
    whence &= ~AVSEEK_FORCE;
    ifstream& file = decoder->file_;
    file.clear();   // a read that reached the end left eof/fail set, which would refuse the seek
    file.seekg(offset, whence == SEEK_CUR ? ios::cur : whence == SEEK_END ? ios::end : ios::beg);
    if (!file) return AVERROR(EIO);
    return static_cast<int64_t>(file.tellg());
}

void M4aDecoder::close() {
    frame_.reset();
    packet_.reset();
    codec_.reset();
    format_.reset();
    io_.reset();
    if (file_.is_open()) file_.close();
    file_.clear();
}

bool M4aDecoder::start(string& error) {
    close();
    stream_index_ = -1;
    sample_format_ = AV_SAMPLE_FMT_NONE;
    frame_offset_ = 0;
    decoded_frames_ = delivered_frames_ = declared_frames_ = 0;
    length_mismatch_.clear();
    mismatch_was_short_ = false;
    at_end_ = false;
    read_error_.clear();
    info_ = AudioInfo{};
    tags_ = Tags{};

    file_.open(path_, ios::binary);
    if (!file_) { error = "cannot open file"; return false; }
    error_code size_error;
    file_size_ = static_cast<int64_t>(fs::file_size(path_, size_error));
    if (size_error) { error = "cannot read file size: " + size_error.message(); return false; }

    constexpr int kIoBufferSize = 64 * 1024;
    auto* buffer = static_cast<unsigned char*>(av_malloc(kIoBufferSize));
    if (!buffer) { error = "out of memory"; return false; }
    io_.reset(avio_alloc_context(buffer, kIoBufferSize, 0, this, &M4aDecoder::read_packet, nullptr, &M4aDecoder::seek));
    if (!io_) { av_free(buffer); error = "out of memory"; return false; }

    // avformat_open_input frees the context itself on failure, so it's only handed to format_
    // once that call has succeeded.
    AVFormatContext* context = avformat_alloc_context();
    if (!context) { error = "out of memory"; return false; }
    context->pb = io_.get();
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    // Refuse any further file the demuxer might ask for (an MP4 can reference external media):
    // this file is the only input, and nothing is ever fetched over a network.
    context->io_open = [](AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) { return AVERROR(EPERM); };
    if (int result = avformat_open_input(&context, nullptr, av_find_input_format("mov"), nullptr); result < 0) {
        error = "cannot read MP4 container: " + av_error_text(result);
        return false;
    }
    format_.reset(context);

    stream_index_ = av_find_best_stream(format_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) { error = "no audio track in MP4 container"; return false; }
    for (unsigned index = 0; index < format_->nb_streams; ++index)
        if (static_cast<int>(index) != stream_index_) format_->streams[index]->discard = AVDISCARD_ALL;   // cover art and the like
    AVStream* stream = format_->streams[stream_index_];

    // Named decoders, never "whatever FFmpeg prefers for this codec": on macOS FFmpeg can also
    // decode AAC through AudioToolbox, and the same file must convert the same way on every OS.
    AVCodecID codec_id = stream->codecpar->codec_id;
    const char* decoder_name = codec_id == AV_CODEC_ID_AAC ? "aac" : codec_id == AV_CODEC_ID_ALAC ? "alac" : nullptr;
    if (!decoder_name) {
        error = string("unsupported audio codec in MP4 container: ") + avcodec_get_name(codec_id) + " (AAC and ALAC are supported)";
        return false;
    }
    const AVCodec* decoder = avcodec_find_decoder_by_name(decoder_name);
    if (!decoder) { error = string("this build of FFmpeg has no ") + decoder_name + " decoder"; return false; }
    codec_.reset(avcodec_alloc_context3(decoder));
    if (!codec_) { error = "out of memory"; return false; }
    if (int result = avcodec_parameters_to_context(codec_.get(), stream->codecpar); result < 0) {
        error = "cannot set up the decoder: " + av_error_text(result);
        return false;
    }
    codec_->pkt_timebase = stream->time_base;
    codec_->thread_count = 1;   // files are already decoded in parallel, one per job (--jobs)
    if (int result = avcodec_open2(codec_.get(), decoder, nullptr); result < 0) {
        error = string("cannot open the ") + decoder_name + " decoder: " + av_error_text(result);
        return false;
    }
    packet_.reset(av_packet_alloc());
    frame_.reset(av_frame_alloc());
    if (!packet_ || !frame_) { error = "out of memory"; return false; }

    // The container's header can't be taken at its word for the rate and channel count: HE-AAC
    // decodes to twice the rate its header gives, and parametric stereo to two channels from
    // one. So the first frame is decoded here and describes the stream; read() starts with it.
    if (!next_frame()) {
        error = read_error_.empty() ? "no audio in the stream" : read_error_;
        return false;
    }
    sample_format_ = static_cast<AVSampleFormat>(frame_->format);
    info_.sample_rate = frame_->sample_rate;
    info_.channels = frame_->ch_layout.nb_channels;
    if (info_.sample_rate < 1 || info_.channels < 1) { error = "no audio channels or sample rate"; return false; }
    switch (av_get_packed_sample_fmt(sample_format_)) {
        case AV_SAMPLE_FMT_FLT: info_.bits = 32; info_.is_float = true; break;   // AAC: FlacEncoder writes it as 24-bit, like any float source
        case AV_SAMPLE_FMT_S16: info_.bits = 16; break;
        case AV_SAMPLE_FMT_S32: {
            // ALAC above 16 bits comes left-justified in 32-bit samples; the real width is what the
            // FLAC output keeps (20-bit ALAC is written as 24-bit FLAC, losslessly).
            int raw = codec_->bits_per_raw_sample;
            info_.bits = raw > 0 && raw <= 16 ? 16 : raw > 0 && raw <= 24 ? 24 : 32;
            break;
        }
        default:
            error = string("unsupported decoded sample format: ") + av_get_sample_fmt_name(sample_format_);
            return false;
    }
    if (stream->duration > 0)
        declared_frames_ = av_rescale_q(stream->duration, stream->time_base, AVRational{1, info_.sample_rate});
    info_.frames = declared_frames_ > 0 ? declared_frames_
                 : format_->duration > 0 ? av_rescale(format_->duration, info_.sample_rate, AV_TIME_BASE) : 0;

    const AVDictionary* metadata = format_->metadata;   // iTunes-style tags ('ilst') live at the container level
    tags_.title = metadata_string(metadata, "title");
    tags_.artist = metadata_string(metadata, "artist");
    tags_.album = metadata_string(metadata, "album");
    tags_.date = metadata_string(metadata, "date");
    tags_.track = metadata_string(metadata, "track");
    tags_.genre = metadata_string(metadata, "genre");
    tags_.comment = metadata_string(metadata, "comment");
    return true;
}

bool M4aDecoder::fail(const string& what, int av_error) {
    read_error_ = what + " (" + av_error_text(av_error) + ")";
    return false;
}

// Makes frame_ the next decoded frame, reading and decoding packets as needed. False at the end
// of the stream or on an error, which read_error_ then describes.
bool M4aDecoder::next_frame() {
    if (at_end_ || !read_error_.empty()) return false;
    for (;;) {
        int result = avcodec_receive_frame(codec_.get(), frame_.get());
        if (result == 0) {
            if (frame_->nb_samples <= 0) continue;
            if (sample_format_ != AV_SAMPLE_FMT_NONE &&
                (frame_->format != sample_format_ || frame_->sample_rate != info_.sample_rate || frame_->ch_layout.nb_channels != info_.channels)) {
                read_error_ = "the audio format changes partway through the stream";
                return false;
            }
            // Every frame should hold exactly the samples the container assigned its packet --
            // FFmpeg keeps the two in step even while it trims the encoder's priming. A damaged
            // packet the decoder doesn't notice can still decode to the wrong length (13% of
            // garbage-filled ALAC packets did, when measured), and this is what catches it. The
            // stream's last frame is the one legitimate exception: it runs long by the encoder's
            // padding, which read() drops. So a mismatch counts only once another frame follows
            // it, or at the end if the last frame came up short rather than long.
            if (!length_mismatch_.empty()) {
                read_error_ = length_mismatch_;
                return false;
            }
            const int64_t expected = frame_->duration > 0
                ? av_rescale_q(frame_->duration, codec_->pkt_timebase, AVRational{1, frame_->sample_rate})
                : frame_->nb_samples;
            if (frame_->nb_samples != expected) {
                length_mismatch_ = "the audio data is damaged (a packet decoded to " + to_string(frame_->nb_samples) +
                                   " samples where the file says " + to_string(expected) + ")";
                mismatch_was_short_ = frame_->nb_samples < expected;
            }
            frame_offset_ = 0;
            decoded_frames_ += frame_->nb_samples;
            return true;
        }
        if (result == AVERROR_EOF) break;
        if (result != AVERROR(EAGAIN)) return fail("the audio data is damaged", result);

        // The decoder needs input: the next packet of the audio track, or once the demuxer has
        // run out, the end-of-stream flush that makes the decoder give up its last frames.
        result = av_read_frame(format_.get(), packet_.get());
        if (result == AVERROR_EOF) {
            result = avcodec_send_packet(codec_.get(), nullptr);
            if (result < 0 && result != AVERROR_EOF) return fail("the audio data is damaged", result);
            continue;
        }
        if (result < 0) return fail("the MP4 container is damaged", result);
        if (packet_->stream_index == stream_index_ && (packet_->flags & AV_PKT_FLAG_CORRUPT)) {
            // The demuxer could only read part of this packet (the file ends inside it): refuse
            // it rather than hand the decoder half a packet and hope it complains.
            av_packet_unref(packet_.get());
            read_error_ = "the MP4 container is damaged (an audio packet is cut short)";
            return false;
        }
        if (packet_->stream_index == stream_index_) result = avcodec_send_packet(codec_.get(), packet_.get());
        av_packet_unref(packet_.get());
        if (result < 0) return fail("the audio data is damaged", result);
    }

    at_end_ = true;
    if (!length_mismatch_.empty() && mismatch_was_short_) {
        read_error_ = length_mismatch_;
        return false;
    }
    // Verification re-decodes the source through this same decoder, so audio missing from the
    // file would be missing from both sides of that comparison and never noticed; only the
    // container's declared length can tell. See kShortfallToleranceSeconds.
    const auto tolerance = static_cast<int64_t>(kShortfallToleranceSeconds * info_.sample_rate);
    if (declared_frames_ > 0 && decoded_frames_ + tolerance < declared_frames_) {
        char message[160];
        snprintf(message, sizeof message, "the audio ends early: %.1f s decoded of %.1f s (truncated or damaged file?)",
                 double(decoded_frames_) / info_.sample_rate, double(declared_frames_) / info_.sample_rate);
        read_error_ = message;
        return false;
    }
    info_.frames = decoded_frames_;   // the declared length was only ever an estimate; this is exact
    return false;
}

template <typename Sample>
int64_t M4aDecoder::read(Sample* out, int64_t frames) {
    // An encoder pads its last frame out to full length, and an M4A's edit list gives the real
    // length so that decoders can drop the padding again. Not every FFmpeg version does that at
    // the end of the stream (6.1 doesn't; the start is always trimmed), so it's done here: the
    // same file then decodes to the same length whichever FFmpeg beatdown was built with.
    if (declared_frames_ > 0) frames = std::min(frames, declared_frames_ - delivered_frames_);
    if (!frame_) return 0;   // a failed seek_start() closed everything; read_error() says why
    int64_t done = 0;
    while (done < frames) {
        if (frame_offset_ >= frame_->nb_samples && !next_frame()) break;
        int take = static_cast<int>(std::min<int64_t>(frames - done, frame_->nb_samples - frame_offset_));
        copy_samples(*frame_, frame_offset_, take, info_.channels, out + done * info_.channels);
        frame_offset_ += take;
        done += take;
    }
    delivered_frames_ += done;
    return done;
}

}  // namespace

unique_ptr<Decoder> open_m4a(const fs::path& path, string& error) {
    silence_ffmpeg_log();
    auto decoder = make_unique<M4aDecoder>(path);
    if (!decoder->start(error)) return nullptr;
    return decoder;
}

}  // namespace beatdown
