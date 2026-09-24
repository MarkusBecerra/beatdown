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
    std::vector<FileResult> files; std::vector<Job> skips; std::vector<std::string> errors; Summary last; SpaceCheck space_seen; bool saw_plan = false; std::mutex m;
    std::vector<std::pair<Job, int64_t>> would_converts;
    void plan(const Plan&, int, const Options&) override { saw_plan = true; }
    void space(const SpaceCheck& s, bool) override { space_seen = s; }
    void file(const FileResult& r) override { std::lock_guard<std::mutex> l(m); files.push_back(r); }
    void skipped(const Job& j) override { skips.push_back(j); }
    void would_convert(const Job& j, int64_t estimated_bytes) override { std::lock_guard<std::mutex> l(m); would_converts.push_back({j, estimated_bytes}); }
    void summary(const Summary& s) override { last = s; }
    void error(const std::string& e) override { errors.push_back(e); }
};

static Options opts(const fs::path& src, const fs::path& dst) { Options o; o.source = src; o.destination = dst; o.jobs = 2; return o; }

// Finding 2 (fix round 1): restores the previous current directory even if a REQUIRE fails and
// unwinds the test case (Catch2 aborts a failed test via a normal C++ exception, so this
// destructor still runs).
struct CurrentDirGuard {
    fs::path previous;
    explicit CurrentDirGuard(const fs::path& to) : previous(fs::current_path()) { fs::current_path(to); }
    ~CurrentDirGuard() { std::error_code ec; fs::current_path(previous, ec); }
};

TEST_CASE("run converts, skips, ignores and fails the right files and exits 1 on a failure") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.2});
    make_audio(t.path / "src/sub/b.aiff", {.container = SF_FORMAT_AIFF, .seconds = 0.2});
    write_bytes(t.path / "src/bad.wav", kCorruptWav);
    write_bytes(t.path / "src/notes.txt", "x");
    write_bytes(t.path / "out/a.mp3", "already");
    RecordingReporter rep; std::atomic<bool> cancel{false};
    int code = run(opts(t.path / "src", t.path / "out"), rep, cancel);
    REQUIRE(code == 1);
    REQUIRE(rep.saw_plan);
    REQUIRE(rep.last.converted == 1);
    REQUIRE(rep.last.skipped == 1);
    REQUIRE(rep.last.failed == 1);
    REQUIRE(rep.last.ignored == 1);
    REQUIRE(rep.last.failures.size() == 1);
    REQUIRE(fs::exists(t.path / "out/sub/b.mp3"));
    REQUIRE(read_file(t.path / "out/a.mp3") == "already");
    REQUIRE(rep.last.bytes_out > 0);
}

TEST_CASE("run exits 0 when everything converts and 0 again when everything is skipped") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.2});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    REQUIRE(run(opts(t.path / "src", t.path / "out"), rep, cancel) == 0);
    auto mtime = fs::last_write_time(t.path / "out/a.mp3");
    std::string bytes = read_file(t.path / "out/a.mp3");
    RecordingReporter rep2;
    REQUIRE(run(opts(t.path / "src", t.path / "out"), rep2, cancel) == 0);
    REQUIRE(rep2.last.skipped == 1);
    REQUIRE(rep2.last.converted == 0);
    REQUIRE(read_file(t.path / "out/a.mp3") == bytes);
    // A6: wrapped in double parens so Catch2 doesn't try to decompose and stringify the
    // fs::file_time_type operands (__int128 duration rep -> ambiguous operator<< on this toolchain).
    REQUIRE((fs::last_write_time(t.path / "out/a.mp3") == mtime));
}

TEST_CASE("run creates the destination's last component only") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.1});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    REQUIRE(run(opts(t.path / "src", t.path / "newdir"), rep, cancel) == 0);
    REQUIRE(fs::is_directory(t.path / "newdir"));
    RecordingReporter rep2;
    REQUIRE(run(opts(t.path / "src", t.path / "Volumes/LaCie/Music"), rep2, cancel) == 2);
    REQUIRE_FALSE(fs::exists(t.path / "Volumes"));
    REQUIRE(rep2.errors.size() == 1);
}

// Finding 2 (fix round 1): a bare relative destination like "Release" has an empty
// parent_path(), which R25's validation would otherwise mistake for a missing/unmounted parent
// and refuse with exit 2. The runner must resolve both source and destination against the
// current directory before checking anything.
TEST_CASE("run resolves a relative source and destination against the current directory") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.1});
    CurrentDirGuard cwd(t.path);
    RecordingReporter rep; std::atomic<bool> cancel{false};
    REQUIRE(run(opts("src", "Release"), rep, cancel) == 0);
    REQUIRE(fs::exists(t.path / "Release" / "a.mp3"));
}

TEST_CASE("run --dry-run writes nothing and reports the projection") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.5});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    Options o = opts(t.path / "src", t.path / "out"); o.dry_run = true;
    REQUIRE(run(o, rep, cancel) == 0);
    REQUIRE(rep.space_seen.needed > 0);
    REQUIRE_FALSE(fs::exists(t.path / "out/a.mp3"));
    REQUIRE(rep.last.converted == 0);
    // A2: exactly one would_convert entry with a positive estimate, no file() calls, and the
    // summary flagged as a dry run.
    REQUIRE(rep.last.dry_run);
    REQUIRE(rep.last.would_convert == 1);
    REQUIRE(rep.files.empty());
    REQUIRE(rep.would_converts.size() == 1);
    REQUIRE(rep.would_converts[0].second > 0);
}

// Finding 3(a) (fix round 1): a new destination given with a trailing slash has no filename
// component, so a naive single parent_path() lands back on the non-existent destination itself;
// the space check must measure its real parent (t.path) instead, which exists and has plenty of
// room for one tiny file.
TEST_CASE("run --dry-run measures the parent of a new trailing-slash destination, not the destination itself") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.2});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    Options o = opts(t.path / "src", fs::path((t.path / "NewRelease").string() + "/"));
    o.dry_run = true;
    REQUIRE(run(o, rep, cancel) == 0);
    // Positive evidence that available_bytes actually measured a real, existing directory (t.path)
    // rather than the space check being silently skipped: a default-constructed SpaceCheck (never
    // set by rep.space()) also has ok == true and available == 0, so checking .ok alone would not
    // catch the space check having been skipped entirely, e.g. by mis-resolving the non-existent
    // "NewRelease" itself and treating that as an unmeasurable directory.
    REQUIRE(rep.space_seen.available > 0);
    REQUIRE(rep.space_seen.ok);
    REQUIRE(rep.errors.empty());
    REQUIRE_FALSE(fs::exists(t.path / "NewRelease"));
}

TEST_CASE("run refuses when the free-space estimate is not met") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.5});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    Options o = opts(t.path / "src", t.path / "out");
    o.encode.format = Format::Flac;
    o.space_override_available = 10;   // test hook: pretend only 10 bytes are free
    REQUIRE(run(o, rep, cancel) == 1);
    REQUIRE_FALSE(fs::exists(t.path / "out/a.flac"));
    // Finding 3(b): R28 refuses before writing anything, including creating the destination
    // folder itself — the old ordering created it before checking space.
    REQUIRE_FALSE(fs::exists(t.path / "out"));
    REQUIRE_FALSE(rep.errors.empty());
}

// Finding 3(c): a dry run previews exactly what the real run would do, including refusing for
// lack of space — it must no longer exit 0 while reporting "Not enough space".
TEST_CASE("run --dry-run also refuses when the free-space estimate is not met") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.5});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    Options o = opts(t.path / "src", t.path / "out");
    o.dry_run = true;
    o.space_override_available = 10;   // test hook: pretend only 10 bytes are free
    REQUIRE(run(o, rep, cancel) == 1);
    REQUIRE_FALSE(rep.space_seen.ok);
    REQUIRE_FALSE(fs::exists(t.path / "out"));
}

TEST_CASE("run stops launching after a disk-full failure and after cancellation") {
    TempDir t;
    for (int i = 0; i < 6; ++i) make_audio(t.path / ("src/" + std::to_string(i) + ".wav"), {.seconds = 0.1});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    std::atomic<int> calls{0};
    Options o = opts(t.path / "src", t.path / "out"); o.jobs = 1;
    int code = run(o, rep, cancel, [&](const Job& j, const Options&, const std::atomic<bool>&) {
        FileResult r; r.job = j; calls++;
        r.outcome = Outcome::Failed; r.error = "No space left on device"; r.disk_full = true;
        return r;
    });
    REQUIRE(code == 1);
    REQUIRE(calls == 1);
    REQUIRE(rep.last.failed == 1);
    // A3: the disk-full stop must be visible on the summary, and every not-yet-started file counts
    // as cancelled.
    REQUIRE(rep.last.disk_full);
    REQUIRE(rep.last.cancelled == 5);

    RecordingReporter rep2; std::atomic<bool> cancel2{false}; std::atomic<int> calls2{0};
    int code2 = run(o, rep2, cancel2, [&](const Job& j, const Options&, const std::atomic<bool>&) {
        FileResult r; r.job = j; if (++calls2 == 2) cancel2 = true; r.outcome = Outcome::Converted; return r;
    });
    REQUIRE(code2 == 130);
    REQUIRE(calls2 == 2);
    REQUIRE(rep2.last.interrupted);
}

// Two sources, one output: the first in name order (track.aiff) is converted, the other is
// skipped, and the output really is the kept source's audio (1 s, not the 3 s WAV).
TEST_CASE("run converts one of two sources that share an output and reports the other as skipped") {
    TempDir t;
    make_audio(t.path / "src/track.aiff", {.container = SF_FORMAT_AIFF, .seconds = 1.0});
    make_audio(t.path / "src/track.wav", {.seconds = 3.0});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    REQUIRE(run(opts(t.path / "src", t.path / "out"), rep, cancel) == 0);
    REQUIRE(rep.last.converted == 1);
    REQUIRE(rep.last.skipped == 1);
    REQUIRE(rep.last.failed == 0);
    REQUIRE(rep.skips.size() == 1);
    REQUIRE(rep.skips[0].note == "same output as track.aiff");
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "out/track.mp3", info, err));
    REQUIRE(info.duration_seconds() == Catch::Approx(1.0).margin(0.2));
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(t.path / "out")) names.push_back(e.path().filename().string());
    REQUIRE(names == std::vector<std::string>{"track.mp3"});   // and no temp files left behind
}

// Two spellings that a normalization- or case-insensitive filesystem (APFS) resolves to one file
// defeat scan()'s ASCII-only path_key fold, which only ever compares name bytes: an NFC-spelled
// "Café.wav" next to an NFD-spelled "Café.flac", or an accented "ÉTÉ.wav" next to "été.flac" (not
// ASCII, so the ASCII case fold doesn't touch it either). Portable across filesystems: where the
// two names really are distinct (e.g. Linux ext4), converting the WAV next to the FLAC is
// correct, so the only assertion made unconditionally is that the FLAC's bytes never change; the
// skip is asserted only where the filesystem actually reports the two names as one file.
TEST_CASE("run never lets an output overwrite a source file reached under a differently spelled name") {
    struct Pair { std::string wav, flac; };
    std::vector<Pair> pairs = {
        {"Caf\xC3\xA9.wav", "Cafe\xCC\x81.flac"},          // NFC "Café" vs NFD "Café"
        {"\xC3\x89T\xC3\x89.wav", "\xC3\xA9t\xC3\xA9.flac"} // "ÉTÉ" vs "été"
    };
    for (const Pair& p : pairs) {
        TempDir t;
        fs::path flac_path = t.path / "src" / path_from_utf8(p.flac);
        make_audio(flac_path, {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16, .seconds = 2.0});
        make_audio(t.path / "src" / path_from_utf8(p.wav), {.seconds = 1.0});
        std::string flac_before = read_file(flac_path);

        RecordingReporter rep; std::atomic<bool> cancel{false};
        Options o = opts(t.path / "src", t.path / "src");
        o.encode.format = Format::Flac; o.overwrite = true;
        run(o, rep, cancel);

        // Unconditional: whatever the filesystem's semantics, the pre-existing FLAC is untouched.
        REQUIRE(read_file(flac_path) == flac_before);

        std::string wav_stem = p.wav.substr(0, p.wav.size() - 4);   // strip ".wav"
        fs::path wav_output = t.path / "src" / path_from_utf8(wav_stem + ".flac");
        fs::path wav_source = t.path / "src" / path_from_utf8(p.wav);
        std::error_code ec;
        if (fs::equivalent(wav_output, flac_path, ec)) {
            auto it = std::find_if(rep.skips.begin(), rep.skips.end(),
                                    [&](const Job& j) { return j.source == wav_source; });
            REQUIRE(it != rep.skips.end());
            REQUIRE(it->note == "output would overwrite a source file");
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
    TempDir t;
    fs::path flac_path = t.path / "src" / path_from_utf8("Cafe\xCC\x81.flac");   // NFD "Café"
    make_audio(flac_path, {.container = SF_FORMAT_FLAC, .subtype = SF_FORMAT_PCM_16, .seconds = 2.0});
    fs::path wav_source = t.path / "src" / path_from_utf8("Caf\xC3\xA9.wav");    // NFC "Café"
    make_audio(wav_source, {.seconds = 1.0});
    // Extra same-folder sources with clearly different lengths (so clearly different byte sizes),
    // populating other size buckets in the same canonical-parent bucket as the twin pair above.
    make_audio(t.path / "src/other1.wav", {.seconds = 0.3});
    make_audio(t.path / "src/other2.wav", {.seconds = 0.7});
    make_audio(t.path / "src/other3.wav", {.seconds = 1.5});
    std::string flac_before = read_file(flac_path);

    RecordingReporter rep; std::atomic<bool> cancel{false};
    Options o = opts(t.path / "src", t.path / "src");
    o.encode.format = Format::Flac; o.overwrite = true;
    run(o, rep, cancel);

    // Unconditional: whatever the filesystem's semantics, the pre-existing FLAC is untouched.
    REQUIRE(read_file(flac_path) == flac_before);

    fs::path wav_output = t.path / "src" / path_from_utf8("Caf\xC3\xA9.flac");
    std::error_code ec;
    if (fs::equivalent(wav_output, flac_path, ec)) {
        auto it = std::find_if(rep.skips.begin(), rep.skips.end(),
                                [&](const Job& j) { return j.source == wav_source; });
        REQUIRE(it != rep.skips.end());
        REQUIRE(it->note == "output would overwrite a source file");
    }
}

TEST_CASE("run reports a missing source as a usage error") {
    TempDir t;
    RecordingReporter rep; std::atomic<bool> cancel{false};
    REQUIRE(run(opts(t.path / "nope", t.path / "out"), rep, cancel) == 2);
}

// A4: run_parallel rethrows a job's first exception on the caller; the runner must not let that
// escape from its per-job lambda, or one bad file would abort every other file in the batch.
TEST_CASE("run turns an exception from convert into a failed file and keeps converting the rest") {
    TempDir t;
    for (int i = 0; i < 3; ++i) make_audio(t.path / ("src/" + std::to_string(i) + ".wav"), {.seconds = 0.1});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    int code = run(opts(t.path / "src", t.path / "out"), rep, cancel, [&](const Job& j, const Options&, const std::atomic<bool>&) -> FileResult {
        if (j.source.filename().string() == "1.wav") throw std::runtime_error("boom");
        FileResult r; r.job = j; r.outcome = Outcome::Converted; return r;
    });
    REQUIRE(code == 1);
    REQUIRE(rep.last.failed == 1);
    REQUIRE(rep.last.converted == 2);
    REQUIRE(rep.last.failures.size() == 1);
    REQUIRE_THAT(rep.last.failures[0].error, ContainsSubstring("boom"));
}

// Task 18: Summary.hot counts converted files whose decoded peak is above +1.0 dBFS -- a
// FileResult with peak +2.3 dBFS counts, one with +0.5 dBFS (a typical loud master, per the
// measurement spike) does not.
TEST_CASE("run counts Summary.hot from converted files whose decoded peak is above +1.0 dBFS") {
    TempDir t;
    make_audio(t.path / "src/hot.wav", {.seconds = 0.1});
    make_audio(t.path / "src/warm.wav", {.seconds = 0.1});
    RecordingReporter rep; std::atomic<bool> cancel{false};
    int code = run(opts(t.path / "src", t.path / "out"), rep, cancel, [&](const Job& j, const Options&, const std::atomic<bool>&) -> FileResult {
        FileResult r; r.job = j; r.outcome = Outcome::Converted;
        r.peak_dbfs = j.source.filename().string() == "hot.wav" ? 2.3 : 0.5;
        return r;
    });
    REQUIRE(code == 0);
    REQUIRE(rep.last.converted == 2);
    REQUIRE(rep.last.hot == 1);
}

// Task 18 fix round 1: the round-1 review found mpglib's hip_decode API (used by the original
// content-check design) shares a function-static output buffer across every handle, racing under
// concurrent conversions -- wrong peaks, spurious "audio content" failures, and a crash inside
// III_dequantize_sample under --jobs. verify_content was redesigned to decode through libsndfile's
// mpg123-backed "mpeg" feature instead. This runs a real batch of files at clearly different
// levels through the full pipeline (real encode + real verify_content) once single-threaded and
// once at high concurrency, and checks every file still converts with the same decoded peak.
TEST_CASE("run at high concurrency (--jobs 8) matches single-threaded per-file decoded peaks") {
    TempDir t;
    const int n = 12;
    for (int i = 0; i < n; ++i) {
        double db = (i % 2 == 0) ? -6.0 : -30.0;
        make_audio(t.path / ("src/" + std::to_string(i) + ".wav"), {.seconds = 2.0, .amplitude = amp_for_dbfs(db)});
    }
    RecordingReporter rep1;
    std::atomic<bool> cancel1{false};
    Options o1 = opts(t.path / "src", t.path / "out1");
    o1.jobs = 1;
    REQUIRE(run(o1, rep1, cancel1) == 0);
    REQUIRE(rep1.files.size() == static_cast<size_t>(n));

    RecordingReporter rep8;
    std::atomic<bool> cancel8{false};
    Options o8 = opts(t.path / "src", t.path / "out8");
    o8.jobs = 8;
    REQUIRE(run(o8, rep8, cancel8) == 0);
    REQUIRE(rep8.files.size() == static_cast<size_t>(n));

    std::map<std::string, double> single_threaded_peak;
    for (auto& r : rep1.files) {
        REQUIRE(r.outcome == Outcome::Converted);
        REQUIRE(r.peak_dbfs.has_value());
        single_threaded_peak[path_to_utf8(r.job.source.filename())] = *r.peak_dbfs;
    }
    REQUIRE(single_threaded_peak.size() == static_cast<size_t>(n));

    int compared = 0;
    for (auto& r : rep8.files) {
        REQUIRE(r.outcome == Outcome::Converted);
        REQUIRE(r.peak_dbfs.has_value());
        auto it = single_threaded_peak.find(path_to_utf8(r.job.source.filename()));
        REQUIRE(it != single_threaded_peak.end());
        REQUIRE(*r.peak_dbfs == Catch::Approx(it->second).margin(0.01));
        ++compared;
    }
    REQUIRE(compared == n);
}
