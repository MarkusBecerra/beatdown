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

static string quote(const string& text) {
#ifdef _WIN32
    return "\"" + text + "\"";
#else
    string quoted = "'";
    for (char character : text) quoted += (character == '\'') ? "'\\''" : string(1, character);
    return quoted + "'";
#endif
}

static CliResult run_cli(const vector<string>& args, const fs::path& log) {
    string cmd = quote(BEATDOWN_BIN);
    for (auto& arg : args) cmd += " " + quote(arg);
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
    TempDir temp_dir;
    auto help_result = run_cli({"--help"}, temp_dir.path / "h.log");
    REQUIRE(help_result.code == 0);
    REQUIRE_THAT(help_result.output, ContainsSubstring("--format"));
    auto version_result = run_cli({"--version"}, temp_dir.path / "v.log");
    REQUIRE(version_result.code == 0);
    REQUIRE_THAT(version_result.output, ContainsSubstring("0.1.0"));
}

TEST_CASE("cli: bad arguments exit 2") {
    TempDir temp_dir;
    REQUIRE(run_cli({}, temp_dir.path / "a.log").code == 2);
    REQUIRE(run_cli({temp_dir.path.string(), temp_dir.path.string(), "--bitrate", "300"}, temp_dir.path / "b.log").code == 2);
    REQUIRE(run_cli({temp_dir.path.string(), temp_dir.path.string(), "--bitrate", "8"}, temp_dir.path / "g.log").code == 2);   // MPEG-2 only
    // Task 18 fix round 2: below 128 kbps CBR, LAME omits its own Info/LAME tag when it won't fit
    // one CBR frame, so the content verifier can't certify it reliably (see the task report).
    REQUIRE(run_cli({temp_dir.path.string(), temp_dir.path.string(), "--bitrate", "112"}, temp_dir.path / "g2.log").code == 2);
    REQUIRE(run_cli({temp_dir.path.string(), temp_dir.path.string(), "--bitrate", "192", "--vbr", "0"}, temp_dir.path / "c.log").code == 2);
    REQUIRE(run_cli({temp_dir.path.string(), temp_dir.path.string(), "--format", "ogg"}, temp_dir.path / "d.log").code == 2);
    REQUIRE(run_cli({temp_dir.path.string(), temp_dir.path.string(), "--quiet", "--verbose"}, temp_dir.path / "e.log").code == 2);
    REQUIRE(run_cli({(temp_dir.path / "missing").string(), (temp_dir.path / "out").string()}, temp_dir.path / "f.log").code == 2);
}

TEST_CASE("cli: --bitrate 128 is accepted and converts") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.3});
    auto result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "out").string(), "--bitrate", "128"}, temp_dir.path / "1.log");
    REQUIRE(result.code == 0);
    REQUIRE(fs::exists(temp_dir.path / "out/a.mp3"));
}

TEST_CASE("cli: converts a folder, then skips everything on the second run") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/one.wav", {.seconds = 0.3});
    make_audio(temp_dir.path / "src/sub/two.wav", {.seconds = 0.3});
    auto result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "out").string(), "--jobs", "2"}, temp_dir.path / "1.log");
    REQUIRE(result.code == 0);
    REQUIRE_THAT(result.output, ContainsSubstring("Converted 2, skipped 0, failed 0"));
    REQUIRE(fs::exists(temp_dir.path / "out/one.mp3"));
    REQUIRE(fs::exists(temp_dir.path / "out/sub/two.mp3"));
    auto second_result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "out").string()}, temp_dir.path / "2.log");
    REQUIRE(second_result.code == 0);
    REQUIRE_THAT(second_result.output, ContainsSubstring("Converted 0, skipped 2"));
}

TEST_CASE("cli: a corrupt file fails alone with exit 1 and is named") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/good.wav", {.seconds = 0.2});
    write_bytes(temp_dir.path / "src/Broken Export.wav", kCorruptWav);
    auto result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "out").string()}, temp_dir.path / "1.log");
    REQUIRE(result.code == 1);
    REQUIRE_THAT(result.output, ContainsSubstring("Broken Export.wav"));
    REQUIRE_THAT(result.output, ContainsSubstring("failed 1"));
    REQUIRE(fs::exists(temp_dir.path / "out/good.mp3"));
}

TEST_CASE("cli: --dry-run writes nothing; --format flac and --strip-suffix are accepted") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/simple fact - slipz Mastered_Master.wav", {.seconds = 0.2});
    auto dry_run_result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "out").string(), "--dry-run"}, temp_dir.path / "1.log");
    REQUIRE(dry_run_result.code == 0);
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out/simple fact - slipz Mastered_Master.mp3"));
    auto flac_result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "out").string(), "--format", "flac", "--tag-from-name", "--strip-suffix", " Mastered_Master", "--strip-suffix", "_x"}, temp_dir.path / "2.log");
    REQUIRE(flac_result.code == 0);
    REQUIRE(fs::exists(temp_dir.path / "out/simple fact - slipz Mastered_Master.flac"));
}

TEST_CASE("cli: refuses a destination whose parent is missing") {
    TempDir temp_dir;
    make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.1});
    auto result = run_cli({(temp_dir.path / "src").string(), (temp_dir.path / "Volumes/LaCie/Music").string()}, temp_dir.path / "1.log");
    REQUIRE(result.code == 2);
    REQUIRE_THAT(result.output, ContainsSubstring("does not exist"));
    REQUIRE_FALSE(fs::exists(temp_dir.path / "Volumes"));
}
