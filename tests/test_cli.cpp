#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>
#include <string>
#include <vector>
#include "fixtures.hpp"

#ifndef _WIN32
#include <sys/wait.h>
#endif

// Global scope, like fixtures.hpp -- this file never does `using namespace beatdown;` (it never
// names anything in that namespace), so it can't reach core/std_names.hpp either.
using std::string;
using std::system;
using std::vector;

using Catch::Matchers::ContainsSubstring;

struct CliResult { int code; string output; };

static string quote(const string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    string q = "'";
    for (char c : s) q += (c == '\'') ? "'\\''" : string(1, c);
    return q + "'";
#endif
}

static CliResult run_cli(const vector<string>& args, const fs::path& log) {
    string cmd = quote(BEATDOWN_BIN);
    for (auto& a : args) cmd += " " + quote(a);
    cmd += " > " + quote(log.string()) + " 2>&1";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
    int rc = system(cmd.c_str());
#else
    int raw = system(cmd.c_str());
    int rc = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
    return {rc, read_file(log)};
}

TEST_CASE("cli: --help and --version exit 0") {
    TempDir t;
    auto h = run_cli({"--help"}, t.path / "h.log");
    REQUIRE(h.code == 0);
    REQUIRE_THAT(h.output, ContainsSubstring("--format"));
    auto v = run_cli({"--version"}, t.path / "v.log");
    REQUIRE(v.code == 0);
    REQUIRE_THAT(v.output, ContainsSubstring("0.1.0"));
}

TEST_CASE("cli: bad arguments exit 2") {
    TempDir t;
    REQUIRE(run_cli({}, t.path / "a.log").code == 2);
    REQUIRE(run_cli({t.path.string(), t.path.string(), "--bitrate", "300"}, t.path / "b.log").code == 2);
    REQUIRE(run_cli({t.path.string(), t.path.string(), "--bitrate", "8"}, t.path / "g.log").code == 2);   // MPEG-2 only
    // Task 18 fix round 2: below 128 kbps CBR, LAME omits its own Info/LAME tag when it won't fit
    // one CBR frame, so the content verifier can't certify it reliably (see the task report).
    REQUIRE(run_cli({t.path.string(), t.path.string(), "--bitrate", "112"}, t.path / "g2.log").code == 2);
    REQUIRE(run_cli({t.path.string(), t.path.string(), "--bitrate", "192", "--vbr", "0"}, t.path / "c.log").code == 2);
    REQUIRE(run_cli({t.path.string(), t.path.string(), "--format", "ogg"}, t.path / "d.log").code == 2);
    REQUIRE(run_cli({t.path.string(), t.path.string(), "--quiet", "--verbose"}, t.path / "e.log").code == 2);
    REQUIRE(run_cli({(t.path / "missing").string(), (t.path / "out").string()}, t.path / "f.log").code == 2);
}

TEST_CASE("cli: --bitrate 128 is accepted and converts") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.3});
    auto r = run_cli({(t.path / "src").string(), (t.path / "out").string(), "--bitrate", "128"}, t.path / "1.log");
    REQUIRE(r.code == 0);
    REQUIRE(fs::exists(t.path / "out/a.mp3"));
}

TEST_CASE("cli: converts a folder, then skips everything on the second run") {
    TempDir t;
    make_audio(t.path / "src/one.wav", {.seconds = 0.3});
    make_audio(t.path / "src/sub/two.wav", {.seconds = 0.3});
    auto r = run_cli({(t.path / "src").string(), (t.path / "out").string(), "--jobs", "2"}, t.path / "1.log");
    REQUIRE(r.code == 0);
    REQUIRE_THAT(r.output, ContainsSubstring("Converted 2, skipped 0, failed 0"));
    REQUIRE(fs::exists(t.path / "out/one.mp3"));
    REQUIRE(fs::exists(t.path / "out/sub/two.mp3"));
    auto r2 = run_cli({(t.path / "src").string(), (t.path / "out").string()}, t.path / "2.log");
    REQUIRE(r2.code == 0);
    REQUIRE_THAT(r2.output, ContainsSubstring("Converted 0, skipped 2"));
}

TEST_CASE("cli: a corrupt file fails alone with exit 1 and is named") {
    TempDir t;
    make_audio(t.path / "src/good.wav", {.seconds = 0.2});
    write_bytes(t.path / "src/Broken Export.wav", kCorruptWav);
    auto r = run_cli({(t.path / "src").string(), (t.path / "out").string()}, t.path / "1.log");
    REQUIRE(r.code == 1);
    REQUIRE_THAT(r.output, ContainsSubstring("Broken Export.wav"));
    REQUIRE_THAT(r.output, ContainsSubstring("failed 1"));
    REQUIRE(fs::exists(t.path / "out/good.mp3"));
}

TEST_CASE("cli: --dry-run writes nothing; --format flac and --strip-suffix are accepted") {
    TempDir t;
    make_audio(t.path / "src/simple fact - slipz Mastered_Master.wav", {.seconds = 0.2});
    auto d = run_cli({(t.path / "src").string(), (t.path / "out").string(), "--dry-run"}, t.path / "1.log");
    REQUIRE(d.code == 0);
    REQUIRE_FALSE(fs::exists(t.path / "out/simple fact - slipz Mastered_Master.mp3"));
    auto f = run_cli({(t.path / "src").string(), (t.path / "out").string(), "--format", "flac", "--tag-from-name", "--strip-suffix", " Mastered_Master", "--strip-suffix", "_x"}, t.path / "2.log");
    REQUIRE(f.code == 0);
    REQUIRE(fs::exists(t.path / "out/simple fact - slipz Mastered_Master.flac"));
}

TEST_CASE("cli: refuses a destination whose parent is missing") {
    TempDir t;
    make_audio(t.path / "src/a.wav", {.seconds = 0.1});
    auto r = run_cli({(t.path / "src").string(), (t.path / "Volumes/LaCie/Music").string()}, t.path / "1.log");
    REQUIRE(r.code == 2);
    REQUIRE_THAT(r.output, ContainsSubstring("does not exist"));
    REQUIRE_FALSE(fs::exists(t.path / "Volumes"));
}
