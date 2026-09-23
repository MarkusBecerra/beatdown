#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <sstream>
#include "core/report.hpp"

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
