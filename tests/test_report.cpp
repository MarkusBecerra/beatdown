#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <sstream>
#include "core/report.hpp"
#include "core/unicode.hpp"

using namespace beatdown;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("format_size uses decimal units with one decimal") {
    REQUIRE(format_size(71'200'000) == "71.2 MB");
    REQUIRE(format_size(812'000) == "812.0 kB");
    REQUIRE(format_size(3'100'000'000) == "3.1 GB");
    REQUIRE(format_size(0) == "0.0 kB");
}

TEST_CASE("format_secs and format_clock") {
    REQUIRE(format_secs(6400ms) == "6.4s");
    REQUIRE(format_clock(19'000ms) == "0:19");
    REQUIRE(format_clock(723'000ms) == "12:03");
}

TEST_CASE("Summary::exit_code") {
    Summary s;
    REQUIRE(s.exit_code() == 0);
    s.failed = 1;
    REQUIRE(s.exit_code() == 1);
    s.failed = 0; s.interrupted = true;
    REQUIRE(s.exit_code() == 130);
}

TEST_CASE("ConsoleReporter prints per-file lines and a summary; quiet keeps only failures and summary") {
    FileResult ok; ok.job.source = "a.wav"; ok.job.output = "/out/a.mp3"; ok.job.source_bytes = 71'200'000; ok.output_bytes = 15'900'000; ok.outcome = Outcome::Converted; ok.elapsed = 6400ms;
    FileResult bad; bad.job.source = "/src/b.wav"; bad.job.output = "/out/b.mp3"; bad.outcome = Outcome::Failed; bad.error = "RIFF header truncated";
    Summary s; s.converted = 1; s.failed = 1; s.ignored = 2; s.bytes_in = 812'400'000; s.bytes_out = 178'600'000; s.elapsed = 19'000ms; s.failures = {bad};

    std::ostringstream out;
    ConsoleReporter r(out, false, false);
    r.file(ok); r.file(bad); r.summary(s);
    std::string text = out.str();
    REQUIRE_THAT(text, ContainsSubstring("✓ a.mp3"));
    REQUIRE_THAT(text, ContainsSubstring("71.2 MB → 15.9 MB"));
    REQUIRE_THAT(text, ContainsSubstring("6.4s"));
    REQUIRE_THAT(text, ContainsSubstring("✗ b.wav"));
    REQUIRE_THAT(text, ContainsSubstring("RIFF header truncated"));
    REQUIRE_THAT(text, ContainsSubstring("Converted 1, skipped 0, failed 1, ignored 2 non-audio files"));
    REQUIRE_THAT(text, ContainsSubstring("Size 812.4 MB → 178.6 MB (−78%)"));
    REQUIRE_THAT(text, ContainsSubstring("Elapsed 0:19"));

    std::ostringstream q;
    ConsoleReporter quiet(q, true, false);
    quiet.file(ok); quiet.file(bad); quiet.summary(s);
    REQUIRE_THAT(q.str(), !ContainsSubstring("✓"));
    REQUIRE_THAT(q.str(), ContainsSubstring("✗ b.wav"));
    REQUIRE_THAT(q.str(), ContainsSubstring("Converted 1"));
}

// A2: dry-run projection line and summary wording.
TEST_CASE("ConsoleReporter prints a dry-run projection and a dry-run summary") {
    Job j; j.source = "/src/a.wav"; j.output = "/out/a.mp3"; j.source_bytes = 71'200'000;
    Summary s; s.dry_run = true; s.would_convert = 1; s.skipped = 0; s.ignored = 0; s.elapsed = 1000ms;

    std::ostringstream out;
    ConsoleReporter r(out, false, false);
    r.would_convert(j, 15'900'000);
    r.summary(s);
    std::string text = out.str();
    REQUIRE_THAT(text, ContainsSubstring("→ a.mp3"));
    REQUIRE_THAT(text, ContainsSubstring("71.2 MB → ~15.9 MB"));
    REQUIRE_THAT(text, ContainsSubstring("Dry run: would convert 1, skip 0, ignore 0 non-audio files"));

    std::ostringstream q;
    ConsoleReporter quiet(q, true, false);
    quiet.would_convert(j, 15'900'000);
    REQUIRE_THAT(q.str(), !ContainsSubstring("→"));
}

// A3: a disk-full stop is called out distinctly from a plain Ctrl-C interruption.
TEST_CASE("ConsoleReporter prints a disk-full early stop in the summary") {
    Summary s; s.failed = 1; s.cancelled = 5; s.disk_full = true; s.elapsed = 2000ms;
    std::ostringstream out;
    ConsoleReporter r(out, false, false);
    r.summary(s);
    REQUIRE_THAT(out.str(), ContainsSubstring("Stopped early: destination disk is full — 5 not converted"));
}

// Finding 1 (fix round 1): R14 requires every failure be reported with its reason; a 500-file
// batch shouldn't force scrolling back through per-file lines to find which ones failed, so
// summary() recaps them, with a path relative to the source root learned from plan().
TEST_CASE("ConsoleReporter recaps every failure at the end of the summary, relative to the source root") {
    Options o; o.source = "/audio-src";
    Plan p;
    std::ostringstream out;
    ConsoleReporter r(out, false, false);
    r.plan(p, 2, o);

    FileResult bad; bad.job.source = "/audio-src/sub/bad.wav"; bad.outcome = Outcome::Failed; bad.error = "RIFF header truncated";
    Summary s; s.failed = 1; s.failures = {bad};
    r.summary(s);

    std::string text = out.str();
    REQUIRE_THAT(text, ContainsSubstring("Failed:"));
    // Built with the platform's separator: fs::relative yields "sub\bad.wav" on Windows.
    std::string rel = path_to_utf8(std::filesystem::path("sub") / "bad.wav");
    REQUIRE_THAT(text, ContainsSubstring("✗ " + rel + "    RIFF header truncated"));
}

// Finding 1: the recap is printed in --quiet too (R20: "--quiet prints only the summary and
// failures") since summary() as a whole was never gated on quiet_.
TEST_CASE("ConsoleReporter prints the failure recap under --quiet") {
    Options o; o.source = "/audio-src";
    Plan p;
    std::ostringstream out;
    ConsoleReporter r(out, true, false);
    r.plan(p, 1, o);

    FileResult bad; bad.job.source = "/audio-src/bad.wav"; bad.outcome = Outcome::Failed; bad.error = "boom";
    Summary s; s.failed = 1; s.failures = {bad};
    r.summary(s);
    REQUIRE_THAT(out.str(), ContainsSubstring("Failed:"));
    REQUIRE_THAT(out.str(), ContainsSubstring("✗ bad.wav    boom"));
}

// Finding 4 (fix round 1): skipped() must respect --quiet like every other per-item line; it
// previously ignored quiet_ entirely, so --quiet --dry-run printed skip lines while
// would_convert() (correctly) stayed silent under quiet — the reverse of R20.
TEST_CASE("ConsoleReporter gates the skip line on quiet and verbose/dry-run correctly") {
    Job j; j.source = "/src/a.wav"; j.note = "output exists";
    Plan p;
    Options dry; dry.dry_run = true;
    Options not_dry;   // dry_run = false (default)

    std::ostringstream quiet_dry;
    ConsoleReporter r1(quiet_dry, true, false);
    r1.plan(p, 1, dry);
    r1.skipped(j);
    REQUIRE_THAT(quiet_dry.str(), !ContainsSubstring("skipped:"));

    std::ostringstream nonquiet_dry;
    ConsoleReporter r2(nonquiet_dry, false, false);
    r2.plan(p, 1, dry);
    r2.skipped(j);
    REQUIRE_THAT(nonquiet_dry.str(), ContainsSubstring("= a.wav    skipped: output exists"));

    std::ostringstream plain;
    ConsoleReporter r3(plain, false, false);   // not quiet, not verbose
    r3.plan(p, 1, not_dry);
    r3.skipped(j);
    REQUIRE_THAT(plain.str(), !ContainsSubstring("skipped:"));
}
