#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include "core/mp3_parse.hpp"
#include "core/runner.hpp"
#include "core/unicode.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

struct RecordingReporter : Reporter {
    vector<FileResult> files; vector<Job> skips; vector<string> errors; Summary last; SpaceCheck space_seen; bool saw_plan = false; mutex mutex_instance;
    vector<pair<Job, int64_t>> would_converts;
    void plan(const Plan&, int, const Options&) override { saw_plan = true; }
    void space(const SpaceCheck& space_check, bool) override { space_seen = space_check; }
    void file(const FileResult& result) override { lock_guard<mutex> lock(mutex_instance); files.push_back(result); }
    void skipped(const Job& job) override { skips.push_back(job); }
    void would_convert(const Job& job, int64_t estimated_bytes) override { lock_guard<mutex> lock(mutex_instance); would_converts.push_back({job, estimated_bytes}); }
    void summary(const Summary& summary_data) override { last = summary_data; }
    void error(const string& message) override { errors.push_back(message); }
};

static Options make_options(const fs::path& source, const fs::path& destination) { Options options; options.source = source; options.destination = destination; options.jobs = 2; return options; }

// Finding 2 (fix round 1): restores the previous current directory even if a REQUIRE fails and
// unwinds the test case (Catch2 aborts a failed test via a normal C++ exception, so this
// destructor still runs).
struct CurrentDirGuard {
    fs::path previous;
    explicit CurrentDirGuard(const fs::path& to) : previous(fs::current_path()) { fs::current_path(to); }
    ~CurrentDirGuard() { error_code fs_error; fs::current_path(previous, fs_error); }
};

TEST_CASE("run converts, skips, ignores and fails the right files and exits 1 on a failure") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.2});
    make_audio(temp_dir.path / "src/sub/b.aiff", {.container = SF_FORMAT_AIFF, .seconds = 0.2});
    write_bytes(temp_dir.path / "src/bad.wav", kCorruptWav);
    write_bytes(temp_dir.path / "src/notes.txt", "x");
    write_bytes(temp_dir.path / "out/a.mp3", "already");
    RecordingReporter reporter; atomic<bool> cancel{false};
    int code = run(make_options(temp_dir.path / "src", temp_dir.path / "out"), reporter, cancel);
    REQUIRE(code == 1);
    REQUIRE(reporter.saw_plan);
    REQUIRE(reporter.last.converted == 1);
    REQUIRE(reporter.last.skipped == 1);
    REQUIRE(reporter.last.failed == 1);
    REQUIRE(reporter.last.ignored == 1);
    REQUIRE(reporter.last.failures.size() == 1);
    REQUIRE(fs::exists(temp_dir.path / "out/sub/b.mp3"));
    REQUIRE(read_file(temp_dir.path / "out/a.mp3") == "already");
    REQUIRE(reporter.last.bytes_out > 0);
}

TEST_CASE("run exits 0 when everything converts and 0 again when everything is skipped") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.2});
    RecordingReporter reporter; atomic<bool> cancel{false};
    REQUIRE(run(make_options(temp_dir.path / "src", temp_dir.path / "out"), reporter, cancel) == 0);
    auto mtime = fs::last_write_time(temp_dir.path / "out/a.mp3");
    string bytes = read_file(temp_dir.path / "out/a.mp3");
    RecordingReporter second_reporter;
    REQUIRE(run(make_options(temp_dir.path / "src", temp_dir.path / "out"), second_reporter, cancel) == 0);
    REQUIRE(second_reporter.last.skipped == 1);
    REQUIRE(second_reporter.last.converted == 0);
    REQUIRE(read_file(temp_dir.path / "out/a.mp3") == bytes);
    // A6: wrapped in double parens so Catch2 doesn't try to decompose and stringify the
    // fs::file_time_type operands (__int128 duration rep -> ambiguous operator<< on this toolchain).
    REQUIRE((fs::last_write_time(temp_dir.path / "out/a.mp3") == mtime));
}

TEST_CASE("run creates the destination's last component only") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.1});
    RecordingReporter reporter; atomic<bool> cancel{false};
    REQUIRE(run(make_options(temp_dir.path / "src", temp_dir.path / "newdir"), reporter, cancel) == 0);
    REQUIRE(fs::is_directory(temp_dir.path / "newdir"));
    RecordingReporter second_reporter;
    REQUIRE(run(make_options(temp_dir.path / "src", temp_dir.path / "Volumes/LaCie/Music"), second_reporter, cancel) == 2);
    REQUIRE_FALSE(fs::exists(temp_dir.path / "Volumes"));
    REQUIRE(second_reporter.errors.size() == 1);
}

// Finding 2 (fix round 1): a bare relative destination like "Release" has an empty
// parent_path(), which R25's validation would otherwise mistake for a missing/unmounted parent
// and refuse with exit 2. The runner must resolve both source and destination against the
// current directory before checking anything.
TEST_CASE("run resolves a relative source and destination against the current directory") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.1});
    CurrentDirGuard cwd(temp_dir.path);
    RecordingReporter reporter; atomic<bool> cancel{false};
    REQUIRE(run(make_options("src", "Release"), reporter, cancel) == 0);
    REQUIRE(fs::exists(temp_dir.path / "Release" / "a.mp3"));
}

TEST_CASE("run --dry-run writes nothing and reports the projection") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.5});
    RecordingReporter reporter; atomic<bool> cancel{false};
    Options options = make_options(temp_dir.path / "src", temp_dir.path / "out"); options.dry_run = true;
    REQUIRE(run(options, reporter, cancel) == 0);
    REQUIRE(reporter.space_seen.needed > 0);
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out/a.mp3"));
    REQUIRE(reporter.last.converted == 0);
    // A2: exactly one would_convert entry with a positive estimate, no file() calls, and the
    // summary flagged as a dry run.
    REQUIRE(reporter.last.dry_run);
    REQUIRE(reporter.last.would_convert == 1);
    REQUIRE(reporter.files.empty());
    REQUIRE(reporter.would_converts.size() == 1);
    REQUIRE(reporter.would_converts[0].second > 0);
}

// Finding 3(a) (fix round 1): a new destination given with a trailing slash has no filename
// component, so a naive single parent_path() lands back on the non-existent destination itself;
// the space check must measure its real parent (temp_dir.path) instead, which exists and has plenty of
// room for one tiny file.
TEST_CASE("run --dry-run measures the parent of a new trailing-slash destination, not the destination itself") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.2});
    RecordingReporter reporter; atomic<bool> cancel{false};
    Options options = make_options(temp_dir.path / "src", fs::path((temp_dir.path / "NewRelease").string() + "/"));
    options.dry_run = true;
    REQUIRE(run(options, reporter, cancel) == 0);
    // Positive evidence that available_bytes actually measured a real, existing directory (temp_dir.path)
    // rather than the space check being silently skipped: a default-constructed SpaceCheck (never
    // set by rep.space()) also has ok == true and available == 0, so checking .ok alone would not
    // catch the space check having been skipped entirely, e.g. by mis-resolving the non-existent
    // "NewRelease" itself and treating that as an unmeasurable directory.
    REQUIRE(reporter.space_seen.available > 0);
    REQUIRE(reporter.space_seen.ok);
    REQUIRE(reporter.errors.empty());
    REQUIRE_FALSE(fs::exists(temp_dir.path / "NewRelease"));
}

TEST_CASE("run refuses when the free-space estimate is not met") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.5});
    RecordingReporter reporter; atomic<bool> cancel{false};
    Options options = make_options(temp_dir.path / "src", temp_dir.path / "out");
    options.encode.format = Format::Flac;
    options.space_override_available = 10;   // test hook: pretend only 10 bytes are free
    REQUIRE(run(options, reporter, cancel) == 1);
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out/a.flac"));
    // Finding 3(b): R28 refuses before writing anything, including creating the destination
    // folder itself — the old ordering created it before checking space.
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out"));
    REQUIRE_FALSE(reporter.errors.empty());
}

// Finding 3(c): a dry run previews exactly what the real run would do, including refusing for
// lack of space — it must no longer exit 0 while reporting "Not enough space".
TEST_CASE("run --dry-run also refuses when the free-space estimate is not met") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.5});
    RecordingReporter reporter; atomic<bool> cancel{false};
    Options options = make_options(temp_dir.path / "src", temp_dir.path / "out");
    options.dry_run = true;
    options.space_override_available = 10;   // test hook: pretend only 10 bytes are free
    REQUIRE(run(options, reporter, cancel) == 1);
    REQUIRE_FALSE(reporter.space_seen.ok);
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out"));
}

TEST_CASE("run stops launching after a disk-full failure and after cancellation") {
    TempDir temp_dir;
    for (int index = 0; index < 6; ++index) make_audio(temp_dir.path / ("src/" + to_string(index) + ".wav"), {.seconds = 0.1});
    RecordingReporter reporter; atomic<bool> cancel{false};
    atomic<int> calls{0};
    Options options = make_options(temp_dir.path / "src", temp_dir.path / "out"); options.jobs = 1;
    int code = run(options, reporter, cancel, [&](const Job& job, const Options&, const atomic<bool>&) {
        FileResult result; result.job = job; calls++;
        result.outcome = Outcome::Failed; result.error = "No space left on device"; result.disk_full = true;
        return result;
    });
    REQUIRE(code == 1);
    REQUIRE(calls == 1);
    REQUIRE(reporter.last.failed == 1);
    // A3: the disk-full stop must be visible on the summary, and every not-yet-started file counts
    // as cancelled.
    REQUIRE(reporter.last.disk_full);
    REQUIRE(reporter.last.cancelled == 5);

    RecordingReporter second_reporter; atomic<bool> cancel2{false}; atomic<int> calls2{0};
    int code2 = run(options, second_reporter, cancel2, [&](const Job& job, const Options&, const atomic<bool>&) {
        FileResult result; result.job = job; if (++calls2 == 2) cancel2 = true; result.outcome = Outcome::Converted; return result;
    });
    REQUIRE(code2 == 130);
    REQUIRE(calls2 == 2);
    REQUIRE(second_reporter.last.interrupted);
}

// Two sources, one output: the first in name order (track.aiff) is converted, the other is
// skipped, and the output really is the kept source's audio (1 s, not the 3 s WAV).
TEST_CASE("run converts one of two sources that share an output and reports the other as skipped") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/track.aiff", {.container = SF_FORMAT_AIFF, .seconds = 1.0});
    make_audio(temp_dir.path / "src/track.wav", {.seconds = 3.0});
    RecordingReporter reporter; atomic<bool> cancel{false};
    REQUIRE(run(make_options(temp_dir.path / "src", temp_dir.path / "out"), reporter, cancel) == 0);
    REQUIRE(reporter.last.converted == 1);
    REQUIRE(reporter.last.skipped == 1);
    REQUIRE(reporter.last.failed == 0);
    REQUIRE(reporter.skips.size() == 1);
    REQUIRE(reporter.skips[0].note == "same output as track.aiff");
    Mp3Info info; string error_message;
    REQUIRE(parse_mp3(temp_dir.path / "out/track.mp3", info, error_message));
    REQUIRE(info.duration_seconds() == Catch::Approx(1.0).margin(0.2));
    vector<string> names;
    for (const auto& entry : fs::directory_iterator(temp_dir.path / "out")) names.push_back(entry.path().filename().string());
    REQUIRE(names == vector<string>{"track.mp3"});   // and no temp files left behind
}

// Two spellings that a normalization- or case-insensitive filesystem (APFS) resolves to one file
// defeat scan()'s ASCII-only path_key fold, which only ever compares name bytes: an NFC-spelled
// "Café.wav" next to an NFD-spelled "Café.flac", or an accented "ÉTÉ.wav" next to "été.flac" (not
// ASCII, so the ASCII case fold doesn't touch it either). Portable across filesystems: where the
// two names really are distinct (e.g. Linux ext4), converting the WAV next to the FLAC is
// correct, so the only assertion made unconditionally is that the FLAC's bytes never change; the
// skip is asserted only where the filesystem actually reports the two names as one file.
TEST_CASE("run never lets an output overwrite a source file reached under a differently spelled name") {
    struct Pair { string wav, flac; };
    vector<Pair> pairs = {
        {"Caf\xC3\xA9.wav", "Cafe\xCC\x81.flac"},          // NFC "Café" vs NFD "Café"
        {"\xC3\x89T\xC3\x89.wav", "\xC3\xA9t\xC3\xA9.flac"} // "ÉTÉ" vs "été"
    };
    for (const Pair& spelling_pair : pairs) {
        TempDir temp_dir;
        fs::path flac_path = temp_dir.path / "src" / path_from_utf8(spelling_pair.flac);
        make_audio(flac_path, {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16, .seconds = 2.0});
        make_audio(temp_dir.path / "src" / path_from_utf8(spelling_pair.wav), {.seconds = 1.0});
        string flac_before = read_file(flac_path);

        RecordingReporter reporter; atomic<bool> cancel{false};
        Options options = make_options(temp_dir.path / "src", temp_dir.path / "src");
        options.encode.format = Format::Flac; options.overwrite = true;
        run(options, reporter, cancel);

        // Unconditional: whatever the filesystem's semantics, the pre-existing FLAC is untouched.
        REQUIRE(read_file(flac_path) == flac_before);

        string wav_stem = spelling_pair.wav.substr(0, spelling_pair.wav.size() - 4);   // strip ".wav"
        fs::path wav_output = temp_dir.path / "src" / path_from_utf8(wav_stem + ".flac");
        fs::path wav_source = temp_dir.path / "src" / path_from_utf8(spelling_pair.wav);
        error_code fs_error;
        if (fs::equivalent(wav_output, flac_path, fs_error)) {
            auto found_job = find_if(reporter.skips.begin(), reporter.skips.end(),
                                    [&](const Job& job) { return job.source == wav_source; });
            REQUIRE(found_job != reporter.skips.end());
            REQUIRE(found_job->note == "output would overwrite a source file");
        }
    }
}

// Fix round 1 (quadratic-cost finding): overwrites_a_source prefilters each canonical-parent
// bucket by file size before calling fs::equivalent, so an output whose size doesn't match a
// candidate never triggers the (more expensive) identity check against it. This must not cost a
// genuine match: reuses the NFC/NFD "Café" twin pair alongside a few extra same-folder WAVs of
// clearly different lengths, which land in other size buckets and must be skipped over, not let
// the real match slip through.
TEST_CASE("run's overwrite check still finds a filesystem-equal name among differently sized siblings") {
    TempDir temp_dir;
    fs::path flac_path = temp_dir.path / "src" / path_from_utf8("Cafe\xCC\x81.flac");   // NFD "Café"
    make_audio(flac_path, {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16, .seconds = 2.0});
    fs::path wav_source = temp_dir.path / "src" / path_from_utf8("Caf\xC3\xA9.wav");    // NFC "Café"
    make_audio(wav_source, {.seconds = 1.0});
    // Extra same-folder sources with clearly different lengths (so clearly different byte sizes),
    // populating other size buckets in the same canonical-parent bucket as the twin pair above.
    make_audio(temp_dir.path / "src/other1.wav", {.seconds = 0.3});
    make_audio(temp_dir.path / "src/other2.wav", {.seconds = 0.7});
    make_audio(temp_dir.path / "src/other3.wav", {.seconds = 1.5});
    string flac_before = read_file(flac_path);

    RecordingReporter reporter; atomic<bool> cancel{false};
    Options options = make_options(temp_dir.path / "src", temp_dir.path / "src");
    options.encode.format = Format::Flac; options.overwrite = true;
    run(options, reporter, cancel);

    // Unconditional: whatever the filesystem's semantics, the pre-existing FLAC is untouched.
    REQUIRE(read_file(flac_path) == flac_before);

    fs::path wav_output = temp_dir.path / "src" / path_from_utf8("Caf\xC3\xA9.flac");
    error_code fs_error;
    if (fs::equivalent(wav_output, flac_path, fs_error)) {
        auto found_job = find_if(reporter.skips.begin(), reporter.skips.end(),
                                [&](const Job& job) { return job.source == wav_source; });
        REQUIRE(found_job != reporter.skips.end());
        REQUIRE(found_job->note == "output would overwrite a source file");
    }
}

TEST_CASE("run reports a missing source as a usage error") {
    TempDir temp_dir;
    RecordingReporter reporter; atomic<bool> cancel{false};
    REQUIRE(run(make_options(temp_dir.path / "nope", temp_dir.path / "out"), reporter, cancel) == 2);
}

// A4: run_parallel rethrows a job's first exception on the caller; the runner must not let that
// escape from its per-job lambda, or one bad file would abort every other file in the batch.
TEST_CASE("run turns an exception from convert into a failed file and keeps converting the rest") {
    TempDir temp_dir;
    for (int index = 0; index < 3; ++index) make_audio(temp_dir.path / ("src/" + to_string(index) + ".wav"), {.seconds = 0.1});
    RecordingReporter reporter; atomic<bool> cancel{false};
    int code = run(make_options(temp_dir.path / "src", temp_dir.path / "out"), reporter, cancel, [&](const Job& job, const Options&, const atomic<bool>&) -> FileResult {
        if (job.source.filename().string() == "1.wav") throw runtime_error("boom");
        FileResult result; result.job = job; result.outcome = Outcome::Converted; return result;
    });
    REQUIRE(code == 1);
    REQUIRE(reporter.last.failed == 1);
    REQUIRE(reporter.last.converted == 2);
    REQUIRE(reporter.last.failures.size() == 1);
    REQUIRE_THAT(reporter.last.failures[0].error, ContainsSubstring("boom"));
}

// Task 18: Summary.hot counts converted files whose decoded peak is above +1.0 dBFS -- a
// FileResult with peak +2.3 dBFS counts, one with +0.5 dBFS (a typical loud master, per the
// measurement spike) does not.
TEST_CASE("run counts Summary.hot from converted files whose decoded peak is above +1.0 dBFS") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/hot.wav", {.seconds = 0.1});
    make_audio(temp_dir.path / "src/warm.wav", {.seconds = 0.1});
    RecordingReporter reporter; atomic<bool> cancel{false};
    int code = run(make_options(temp_dir.path / "src", temp_dir.path / "out"), reporter, cancel, [&](const Job& job, const Options&, const atomic<bool>&) -> FileResult {
        FileResult result; result.job = job; result.outcome = Outcome::Converted;
        result.peak_dbfs = job.source.filename().string() == "hot.wav" ? 2.3 : 0.5;
        return result;
    });
    REQUIRE(code == 0);
    REQUIRE(reporter.last.converted == 2);
    REQUIRE(reporter.last.hot == 1);
}

// Task 18 fix round 1: the round-1 review found mpglib's hip_decode API (used by the original
// content-check design) shares a function-static output buffer across every handle, racing under
// concurrent conversions -- wrong peaks, spurious "audio content" failures, and a crash inside
// III_dequantize_sample under --jobs. verify_content was redesigned to decode through libsndfile's
// mpg123-backed "mpeg" feature instead. This runs a real batch of files at clearly different
// levels through the full pipeline (real encode + real verify_content) once single-threaded and
// once at high concurrency, and checks every file still converts with the same decoded peak.
TEST_CASE("run at high concurrency (--jobs 8) matches single-threaded per-file decoded peaks") {
    TempDir temp_dir;
    const int file_count = 12;
    for (int index = 0; index < file_count; ++index) {
        double db = (index % 2 == 0) ? -6.0 : -30.0;
        make_audio(temp_dir.path / ("src/" + to_string(index) + ".wav"), {.seconds = 2.0, .amplitude = amp_for_dbfs(db)});
    }
    RecordingReporter single_threaded_reporter;
    atomic<bool> cancel_single{false};
    Options single_threaded_options = make_options(temp_dir.path / "src", temp_dir.path / "out1");
    single_threaded_options.jobs = 1;
    REQUIRE(run(single_threaded_options, single_threaded_reporter, cancel_single) == 0);
    REQUIRE(single_threaded_reporter.files.size() == static_cast<size_t>(file_count));

    RecordingReporter concurrent_reporter;
    atomic<bool> cancel_concurrent{false};
    Options concurrent_options = make_options(temp_dir.path / "src", temp_dir.path / "out8");
    concurrent_options.jobs = 8;
    REQUIRE(run(concurrent_options, concurrent_reporter, cancel_concurrent) == 0);
    REQUIRE(concurrent_reporter.files.size() == static_cast<size_t>(file_count));

    map<string, double> single_threaded_peak;
    for (auto& result : single_threaded_reporter.files) {
        REQUIRE(result.outcome == Outcome::Converted);
        REQUIRE(result.peak_dbfs.has_value());
        single_threaded_peak[path_to_utf8(result.job.source.filename())] = *result.peak_dbfs;
    }
    REQUIRE(single_threaded_peak.size() == static_cast<size_t>(file_count));

    int compared = 0;
    for (auto& result : concurrent_reporter.files) {
        REQUIRE(result.outcome == Outcome::Converted);
        REQUIRE(result.peak_dbfs.has_value());
        auto found_peak = single_threaded_peak.find(path_to_utf8(result.job.source.filename()));
        REQUIRE(found_peak != single_threaded_peak.end());
        REQUIRE(*result.peak_dbfs == Catch::Approx(found_peak->second).margin(0.01));
        ++compared;
    }
    REQUIRE(compared == file_count);
}
