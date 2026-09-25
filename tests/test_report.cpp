#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <limits>
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
    Summary summary;
    REQUIRE(summary.exit_code() == 0);
    summary.failed = 1;
    REQUIRE(summary.exit_code() == 1);
    summary.failed = 0; summary.interrupted = true;
    REQUIRE(summary.exit_code() == 130);
}

TEST_CASE("ConsoleReporter prints per-file lines and a summary; quiet keeps only failures and summary") {
    FileResult ok; ok.job.source = "a.wav"; ok.job.output = "/out/a.mp3"; ok.job.source_bytes = 71'200'000; ok.output_bytes = 15'900'000; ok.outcome = Outcome::Converted; ok.elapsed = 6400ms;
    FileResult bad; bad.job.source = "/src/b.wav"; bad.job.output = "/out/b.mp3"; bad.outcome = Outcome::Failed; bad.error = "RIFF header truncated";
    Summary summary; summary.converted = 1; summary.failed = 1; summary.ignored = 2; summary.bytes_in = 812'400'000; summary.bytes_out = 178'600'000; summary.elapsed = 19'000ms; summary.failures = {bad};

    ostringstream out;
    ConsoleReporter reporter(out, false, false);
    reporter.file(ok); reporter.file(bad); reporter.summary(summary);
    string text = out.str();
    REQUIRE_THAT(text, ContainsSubstring("✓ a.mp3"));
    REQUIRE_THAT(text, ContainsSubstring("71.2 MB → 15.9 MB"));
    REQUIRE_THAT(text, ContainsSubstring("6.4s"));
    REQUIRE_THAT(text, ContainsSubstring("✗ b.wav"));
    REQUIRE_THAT(text, ContainsSubstring("RIFF header truncated"));
    REQUIRE_THAT(text, ContainsSubstring("Converted 1, skipped 0, failed 1, ignored 2 non-audio files"));
    REQUIRE_THAT(text, ContainsSubstring("Size 812.4 MB → 178.6 MB (−78%)"));
    REQUIRE_THAT(text, ContainsSubstring("Elapsed 0:19"));

    ostringstream quiet_out;
    ConsoleReporter quiet(quiet_out, true, false);
    quiet.file(ok); quiet.file(bad); quiet.summary(summary);
    REQUIRE_THAT(quiet_out.str(), !ContainsSubstring("✓"));
    REQUIRE_THAT(quiet_out.str(), ContainsSubstring("✗ b.wav"));
    REQUIRE_THAT(quiet_out.str(), ContainsSubstring("Converted 1"));
}

// A2: dry-run projection line and summary wording.
TEST_CASE("ConsoleReporter prints a dry-run projection and a dry-run summary") {
    Job job; job.source = "/src/a.wav"; job.output = "/out/a.mp3"; job.source_bytes = 71'200'000;
    Summary summary; summary.dry_run = true; summary.would_convert = 1; summary.skipped = 0; summary.ignored = 0; summary.elapsed = 1000ms;

    ostringstream out;
    ConsoleReporter reporter(out, false, false);
    reporter.would_convert(job, 15'900'000);
    reporter.summary(summary);
    string text = out.str();
    REQUIRE_THAT(text, ContainsSubstring("→ a.mp3"));
    REQUIRE_THAT(text, ContainsSubstring("71.2 MB → ~15.9 MB"));
    REQUIRE_THAT(text, ContainsSubstring("Dry run: would convert 1, skip 0, ignore 0 non-audio files"));

    ostringstream quiet_out;
    ConsoleReporter quiet(quiet_out, true, false);
    quiet.would_convert(job, 15'900'000);
    REQUIRE_THAT(quiet_out.str(), !ContainsSubstring("→"));
}

// A3: a disk-full stop is called out distinctly from a plain Ctrl-C interruption.
TEST_CASE("ConsoleReporter prints a disk-full early stop in the summary") {
    Summary summary; summary.failed = 1; summary.cancelled = 5; summary.disk_full = true; summary.elapsed = 2000ms;
    ostringstream out;
    ConsoleReporter reporter(out, false, false);
    reporter.summary(summary);
    REQUIRE_THAT(out.str(), ContainsSubstring("Stopped early: destination disk is full — 5 not converted"));
}

// Finding 1 (fix round 1): R14 requires every failure be reported with its reason; a 500-file
// batch shouldn't force scrolling back through per-file lines to find which ones failed, so
// summary() recaps them, with a path relative to the source root learned from plan().
TEST_CASE("ConsoleReporter recaps every failure at the end of the summary, relative to the source root") {
    Options options; options.source = "/audio-src";
    Plan plan;
    ostringstream out;
    ConsoleReporter reporter(out, false, false);
    reporter.plan(plan, 2, options);

    FileResult bad; bad.job.source = "/audio-src/sub/bad.wav"; bad.outcome = Outcome::Failed; bad.error = "RIFF header truncated";
    Summary summary; summary.failed = 1; summary.failures = {bad};
    reporter.summary(summary);

    string text = out.str();
    REQUIRE_THAT(text, ContainsSubstring("Failed:"));
    // Built with the platform's separator: fs::relative yields "sub\bad.wav" on Windows.
    string relative_path = path_to_utf8(fs::path("sub") / "bad.wav");
    REQUIRE_THAT(text, ContainsSubstring("✗ " + relative_path + "    RIFF header truncated"));
}

// Finding 1: the recap is printed in --quiet too (R20: "--quiet prints only the summary and
// failures") since summary() as a whole was never gated on quiet_.
TEST_CASE("ConsoleReporter prints the failure recap under --quiet") {
    Options options; options.source = "/audio-src";
    Plan plan;
    ostringstream out;
    ConsoleReporter reporter(out, true, false);
    reporter.plan(plan, 1, options);

    FileResult bad; bad.job.source = "/audio-src/bad.wav"; bad.outcome = Outcome::Failed; bad.error = "boom";
    Summary summary; summary.failed = 1; summary.failures = {bad};
    reporter.summary(summary);
    REQUIRE_THAT(out.str(), ContainsSubstring("Failed:"));
    REQUIRE_THAT(out.str(), ContainsSubstring("✗ bad.wav    boom"));
}

// Task 18: with --verbose, a converted MP3's peak dBFS is appended to its ✓ line; without
// --verbose it stays hidden, and a FLAC result (no peak_dbfs) shows neither.
// Test names stay ASCII: ctest passes them to the test binary on the command line, and
// Windows converts that through the ANSI code page, so a non-ASCII name matches no test.
TEST_CASE("ConsoleReporter shows the decoded peak on the verbose converted-file line only") {
    FileResult result; result.job.source = "a.wav"; result.job.output = "/out/a.mp3"; result.outcome = Outcome::Converted; result.peak_dbfs = 0.48;

    ostringstream verbose_out;
    ConsoleReporter verbose(verbose_out, false, true);
    verbose.file(result);
    REQUIRE_THAT(verbose_out.str(), ContainsSubstring("peak +0.48 dBFS"));

    ostringstream plain_out;
    ConsoleReporter plain(plain_out, false, false);
    plain.file(result);
    REQUIRE_THAT(plain_out.str(), !ContainsSubstring("peak"));

    FileResult flac; flac.job.source = "a.wav"; flac.job.output = "/out/a.flac"; flac.outcome = Outcome::Converted;
    ostringstream flac_out;
    ConsoleReporter flac_rep(flac_out, false, true);
    flac_rep.file(flac);
    REQUIRE_THAT(flac_out.str(), !ContainsSubstring("peak"));
}

// Task 18 fix round 1: a genuinely silent decode is -inf dBFS, which isn't a useful number to
// print next to every other file's two-decimal figure.
TEST_CASE("ConsoleReporter prints 'peak: silent' instead of -inf dBFS for a silent decode") {
    FileResult result; result.job.source = "a.wav"; result.job.output = "/out/a.mp3"; result.outcome = Outcome::Converted;
    result.peak_dbfs = -numeric_limits<double>::infinity();
    ostringstream out;
    ConsoleReporter(out, false, true).file(result);
    REQUIRE_THAT(out.str(), ContainsSubstring("peak: silent"));
    REQUIRE_THAT(out.str(), !ContainsSubstring("inf"));
}

// Task 18: the "very loud masters" callout is driven directly by Summary.hot (its aggregation
// from FileResult.peak_dbfs is a runner concern, covered in test_runner.cpp).
TEST_CASE("ConsoleReporter prints the hot-files summary line only when Summary.hot is set") {
    Summary hot; hot.converted = 1; hot.hot = 1;
    ostringstream hot_out;
    ConsoleReporter(hot_out, false, false).summary(hot);
    REQUIRE_THAT(hot_out.str(), ContainsSubstring("1 file(s) decode above +1.0 dBFS — very loud masters; see \"Loud masters\" in the README"));

    Summary warm; warm.converted = 1; warm.hot = 0;
    ostringstream warm_out;
    ConsoleReporter(warm_out, false, false).summary(warm);
    REQUIRE_THAT(warm_out.str(), !ContainsSubstring("very loud masters"));
}

// Finding 4 (fix round 1): skipped() must respect --quiet like every other per-item line; it
// previously ignored quiet_ entirely, so --quiet --dry-run printed skip lines while
// would_convert() (correctly) stayed silent under quiet — the reverse of R20.
TEST_CASE("ConsoleReporter gates the skip line on quiet and verbose/dry-run correctly") {
    Job job; job.source = "/src/a.wav"; job.note = "output exists";
    Plan plan;
    Options dry; dry.dry_run = true;
    Options not_dry;   // dry_run = false (default)

    ostringstream quiet_dry;
    ConsoleReporter quiet_dry_reporter(quiet_dry, true, false);
    quiet_dry_reporter.plan(plan, 1, dry);
    quiet_dry_reporter.skipped(job);
    REQUIRE_THAT(quiet_dry.str(), !ContainsSubstring("skipped:"));

    ostringstream nonquiet_dry;
    ConsoleReporter nonquiet_dry_reporter(nonquiet_dry, false, false);
    nonquiet_dry_reporter.plan(plan, 1, dry);
    nonquiet_dry_reporter.skipped(job);
    REQUIRE_THAT(nonquiet_dry.str(), ContainsSubstring("= a.wav    skipped: output exists"));

    ostringstream plain;
    ConsoleReporter plain_reporter(plain, false, false);   // not quiet, not verbose
    plain_reporter.plan(plan, 1, not_dry);
    plain_reporter.skipped(job);
    REQUIRE_THAT(plain.str(), !ContainsSubstring("skipped:"));
}
