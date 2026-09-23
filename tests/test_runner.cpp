#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include "core/runner.hpp"
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
