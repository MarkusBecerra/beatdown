#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "core/scanner.hpp"
#include "fixtures.hpp"

using namespace beatdown;

static Options opts(const fs::path& src, const fs::path& dst) { Options o; o.source = src; o.destination = dst; return o; }

TEST_CASE("is_audio_input matches extensions case-insensitively") {
    REQUIRE(is_audio_input("a.wav")); REQUIRE(is_audio_input("a.WAV")); REQUIRE(is_audio_input("a.Aiff"));
    REQUIRE(is_audio_input("a.aif")); REQUIRE(is_audio_input("a.flac"));
    REQUIRE_FALSE(is_audio_input("a.mp3")); REQUIRE_FALSE(is_audio_input("a.txt")); REQUIRE_FALSE(is_audio_input("wav"));
}

TEST_CASE("scan mirrors sub-folders and pairs outputs with the format extension") {
    TempDir t;
    write_bytes(t.path / "src/one.wav", "x");
    write_bytes(t.path / "src/Sub Folder/two.AIFF", "x");
    write_bytes(t.path / "src/Sub Folder/deep/three.flac", "x");
    std::string err;
    Plan p = scan(opts(t.path / "src", t.path / "out"), err);
    REQUIRE(err.empty());
    REQUIRE(p.to_convert.size() == 3);
    std::sort(p.to_convert.begin(), p.to_convert.end(), [](auto& a, auto& b) { return a.source < b.source; });
    REQUIRE(p.to_convert[0].output == t.path / "out/Sub Folder/deep/three.mp3");
    REQUIRE(p.to_convert[1].output == t.path / "out/Sub Folder/two.mp3");
    REQUIRE(p.to_convert[2].output == t.path / "out/one.mp3");
    REQUIRE(p.to_convert[2].source_bytes == 1);
}

TEST_CASE("scan --no-recursive stays at the top level") {
    TempDir t;
    write_bytes(t.path / "src/one.wav", "x");
    write_bytes(t.path / "src/sub/two.wav", "x");
    auto o = opts(t.path / "src", t.path / "out"); o.recursive = false;
    std::string err;
    REQUIRE(scan(o, err).to_convert.size() == 1);
}

TEST_CASE("scan ignores non-audio and ._ resource forks, counting them") {
    TempDir t;
    write_bytes(t.path / "src/one.wav", "x");
    write_bytes(t.path / "src/._one.wav", "x");
    write_bytes(t.path / "src/cover.jpg", "x");
    write_bytes(t.path / "src/.DS_Store", "x");
    std::string err;
    Plan p = scan(opts(t.path / "src", t.path / "out"), err);
    REQUIRE(p.to_convert.size() == 1);
    REQUIRE(p.ignored == 3);
}

TEST_CASE("scan skips files whose output exists unless --overwrite") {
    TempDir t;
    write_bytes(t.path / "src/one.wav", "x");
    write_bytes(t.path / "out/one.mp3", "x");
    std::string err;
    Plan p = scan(opts(t.path / "src", t.path / "out"), err);
    REQUIRE(p.to_convert.empty());
    REQUIRE(p.skipped.size() == 1);
    REQUIRE(p.skipped[0].note == "output exists");
    auto o = opts(t.path / "src", t.path / "out"); o.overwrite = true;
    REQUIRE(scan(o, err).to_convert.size() == 1);
}

TEST_CASE("scan accepts a single file source") {
    TempDir t;
    write_bytes(t.path / "one.wav", "x");
    std::string err;
    Plan p = scan(opts(t.path / "one.wav", t.path / "out"), err);
    REQUIRE(p.to_convert.size() == 1);
    REQUIRE(p.to_convert[0].output == t.path / "out/one.mp3");
}

TEST_CASE("scan uses .flac outputs for --format flac and never maps a file onto itself") {
    TempDir t;
    write_bytes(t.path / "src/one.flac", "x");
    auto o = opts(t.path / "src", t.path / "src"); o.encode.format = Format::Flac;
    std::string err;
    Plan p = scan(o, err);
    REQUIRE(p.to_convert.empty());
    REQUIRE(p.skipped.size() == 1);
    REQUIRE(p.skipped[0].note == "output would be the source file");
}

TEST_CASE("scan does not descend into a destination that lives inside the source") {
    TempDir t;
    write_bytes(t.path / "src/one.wav", "x");
    write_bytes(t.path / "src/out/old.flac", "x");
    auto o = opts(t.path / "src", t.path / "src/out"); o.encode.format = Format::Flac;
    std::string err;
    Plan p = scan(o, err);
    REQUIRE(p.to_convert.size() == 1);
    REQUIRE(p.ignored == 0);
}

TEST_CASE("scan reports a missing source") {
    TempDir t;
    std::string err;
    scan(opts(t.path / "nope", t.path / "out"), err);
    REQUIRE_FALSE(err.empty());
}
