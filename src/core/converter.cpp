#include "core/converter.hpp"
#include <cerrno>
#include <cstdio>
#include <limits>
#include <random>
#include <system_error>
#include "core/content_check.hpp"
#include "core/encoder.hpp"
#include "core/space.hpp"
#include "core/std_names.hpp"
#include "core/unicode.hpp"
#include "core/verifier.hpp"

namespace beatdown {

fs::path temp_path_for(const fs::path& output) {
    thread_local mt19937_64 rng{random_device{}()};
    char hex[9];
    snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(rng() & 0xFFFFFFFFu));
    return output.parent_path() / path_from_utf8(".beatdown-" + path_to_utf8(output.filename()) + "." + hex + ".part");
}

Tags resolve_tags(const Decoder& decoder, const Options& options) {
    Tags tags = decoder.tags();
    if (!options.tag_from_name) return tags;
    Tags derived = tags_from_filename(path_to_utf8(decoder.path().stem()));
    if (derived.title) derived.title = strip_suffixes(*derived.title, options.strip_suffixes);
    return merge_tags(tags, derived);
}

bool looks_like_disk_full(const string& error, int64_t available, int64_t estimated) {
    if (available < estimated) return true;
    return error.find("No space left") != string::npos || error.find("not enough space") != string::npos
        || error.find("There is not enough space") != string::npos;
}

FileResult convert_one(const Job& job, const Options& options, const atomic<bool>& cancel) {
    FileResult result;
    result.job = job;
    auto start_time = chrono::steady_clock::now();
    auto done = [&](Outcome outcome, string error_message = "") {
        result.outcome = outcome;
        result.error = std::move(error_message);
        result.elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start_time);
        return result;
    };
    if (cancel.load()) return done(Outcome::Cancelled);

    string error_message;
    auto decoder = Decoder::open(job.source, error_message);
    if (!decoder) return done(Outcome::Failed, error_message);
    // Task 18 fix round 2: a source already coded as MP3 (however it's named -- an MP3 renamed
    // to .wav still decodes as MPEG) is refused outright rather than re-encoded, for either
    // output format: re-encoding lossy audio compounds its losses for no benefit.
    if (decoder->info().is_mpeg()) return done(Outcome::Failed, "source is MP3 data — not re-encoding lossy audio");

    error_code fs_error;
    fs::create_directories(job.output.parent_path(), fs_error);
    if (fs_error) return done(Outcome::Failed, "cannot create " + path_to_utf8(job.output.parent_path()) + ": " + fs_error.message());

    fs::path tmp = temp_path_for(job.output);
    struct Cleanup { const fs::path& path; bool armed = true; ~Cleanup() { if (armed) { error_code ignored_error; fs::remove(path, ignored_error); } } } cleanup{tmp};

    Tags tags = resolve_tags(*decoder, options);
    auto encoder = make_encoder(options.encode);
    string log;
    string encode_error = encoder->encode(*decoder, tmp, tags, cancel, options.verbose ? &log : nullptr);
    result.verbose_log = log;
    if (encode_error == "cancelled") return done(Outcome::Cancelled);
    if (!encode_error.empty()) {
        // A write that fails for lack of space is reported so the runner can stop the batch (R28).
        error_code space_error;
        auto space_info = fs::space(job.output.parent_path(), space_error);
        int64_t estimated = estimate_output_bytes(decoder->info(), options.encode);
        int64_t available = space_error ? numeric_limits<int64_t>::max() : static_cast<int64_t>(space_info.available);
        result.disk_full = looks_like_disk_full(encode_error, available, estimated);
        return done(Outcome::Failed, encode_error);
    }
    string verify_error = verify_output(tmp, options.encode, decoder->info());
    if (!verify_error.empty()) return done(Outcome::Failed, "verification failed: " + verify_error);
    ContentCheckResult content_check = verify_content(tmp, options.encode, job.source);
    if (!content_check.error.empty()) return done(Outcome::Failed, "verification failed: " + content_check.error);
    result.peak_dbfs = content_check.peak_dbfs;

    auto src_mtime = fs::last_write_time(job.source, fs_error);
    if (fs_error) return done(Outcome::Failed, "cannot read source modification time: " + fs_error.message());
    fs::last_write_time(tmp, src_mtime, fs_error);
    if (fs_error) return done(Outcome::Failed, "cannot set modification time: " + fs_error.message());
    // Replaces an existing output (--overwrite) atomically on POSIX and with MSVC's std::filesystem.
    fs::rename(tmp, job.output, fs_error);
    if (fs_error) return done(Outcome::Failed, "cannot rename to " + path_to_utf8(job.output) + ": " + fs_error.message());
    cleanup.armed = false;
    result.output_bytes = static_cast<int64_t>(fs::file_size(job.output, fs_error));
    return done(Outcome::Converted);
}

}  // namespace beatdown
