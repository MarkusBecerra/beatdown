#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include "core/scanner.hpp"
#include "fixtures.hpp"

using namespace beatdown;

static Options make_options(const fs::path& source, const fs::path& destination) { Options options; options.source = source; options.destination = destination; return options; }

TEST_CASE("is_audio_input matches extensions case-insensitively") {
    REQUIRE(is_audio_input("a.wav")); REQUIRE(is_audio_input("a.WAV")); REQUIRE(is_audio_input("a.Aiff"));
    REQUIRE(is_audio_input("a.aif")); REQUIRE(is_audio_input("a.flac"));
    REQUIRE_FALSE(is_audio_input("a.mp3")); REQUIRE_FALSE(is_audio_input("a.txt")); REQUIRE_FALSE(is_audio_input("wav"));
}

TEST_CASE("scan mirrors sub-folders and pairs outputs with the format extension") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/one.wav", "x");
    write_bytes(temp_dir.path / "src/Sub Folder/two.AIFF", "x");
    write_bytes(temp_dir.path / "src/Sub Folder/deep/three.flac", "x");
    string error_message;
    Plan plan = scan(make_options(temp_dir.path / "src", temp_dir.path / "out"), error_message);
    REQUIRE(error_message.empty());
    REQUIRE(plan.to_convert.size() == 3);
    sort(plan.to_convert.begin(), plan.to_convert.end(), [](auto& left, auto& right) { return left.source < right.source; });
    REQUIRE(plan.to_convert[0].output == temp_dir.path / "out/Sub Folder/deep/three.mp3");
    REQUIRE(plan.to_convert[1].output == temp_dir.path / "out/Sub Folder/two.mp3");
    REQUIRE(plan.to_convert[2].output == temp_dir.path / "out/one.mp3");
    REQUIRE(plan.to_convert[2].source_bytes == 1);
}

TEST_CASE("scan --no-recursive stays at the top level") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/one.wav", "x");
    write_bytes(temp_dir.path / "src/sub/two.wav", "x");
    auto options = make_options(temp_dir.path / "src", temp_dir.path / "out"); options.recursive = false;
    string error_message;
    REQUIRE(scan(options, error_message).to_convert.size() == 1);
}

TEST_CASE("scan ignores non-audio and ._ resource forks, counting them") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/one.wav", "x");
    write_bytes(temp_dir.path / "src/._one.wav", "x");
    write_bytes(temp_dir.path / "src/cover.jpg", "x");
    write_bytes(temp_dir.path / "src/.DS_Store", "x");
    string error_message;
    Plan plan = scan(make_options(temp_dir.path / "src", temp_dir.path / "out"), error_message);
    REQUIRE(plan.to_convert.size() == 1);
    REQUIRE(plan.ignored == 3);
}

TEST_CASE("scan skips files whose output exists unless --overwrite") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/one.wav", "x");
    write_bytes(temp_dir.path / "out/one.mp3", "x");
    string error_message;
    Plan plan = scan(make_options(temp_dir.path / "src", temp_dir.path / "out"), error_message);
    REQUIRE(plan.to_convert.empty());
    REQUIRE(plan.skipped.size() == 1);
    REQUIRE(plan.skipped[0].note == "output exists");
    auto options = make_options(temp_dir.path / "src", temp_dir.path / "out"); options.overwrite = true;
    REQUIRE(scan(options, error_message).to_convert.size() == 1);
}

TEST_CASE("scan accepts a single file source") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "one.wav", "x");
    string error_message;
    Plan plan = scan(make_options(temp_dir.path / "one.wav", temp_dir.path / "out"), error_message);
    REQUIRE(plan.to_convert.size() == 1);
    REQUIRE(plan.to_convert[0].output == temp_dir.path / "out/one.mp3");
}

TEST_CASE("scan uses .flac outputs for --format flac and never maps a file onto itself") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/one.flac", "x");
    auto options = make_options(temp_dir.path / "src", temp_dir.path / "src"); options.encode.format = Format::Flac;
    string error_message;
    Plan plan = scan(options, error_message);
    REQUIRE(plan.to_convert.empty());
    REQUIRE(plan.skipped.size() == 1);
    REQUIRE(plan.skipped[0].note == "output would be the source file");
}

static string note_for(const vector<Job>& jobs, const fs::path& source) {
    for (const auto& job : jobs) if (job.source == source) return job.note;
    return "<not in this list>";
}

TEST_CASE("scan keeps the first of two sources that map to the same output and skips the other") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/track.wav", "x");
    write_bytes(temp_dir.path / "src/track.aiff", "x");
    string error_message;
    Plan plan = scan(make_options(temp_dir.path / "src", temp_dir.path / "out"), error_message);
    REQUIRE(plan.to_convert.size() == 1);
    REQUIRE(plan.to_convert[0].source == temp_dir.path / "src/track.aiff");   // sorts first
    REQUIRE(plan.skipped.size() == 1);
    REQUIRE(note_for(plan.skipped, temp_dir.path / "src/track.wav") == "same output as track.aiff");
}

// Track.mp3 and track.mp3 are one file on the case-insensitive volumes macOS and Windows use by
// default. Byte order puts "Track.wav" ('T' = 0x54) before "track.aiff" ('t' = 0x74).
TEST_CASE("scan treats outputs that differ only in letter case as the same output") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/Track.wav", "x");
    write_bytes(temp_dir.path / "src/track.aiff", "x");
    string error_message;
    Plan plan = scan(make_options(temp_dir.path / "src", temp_dir.path / "out"), error_message);
    REQUIRE(plan.to_convert.size() == 1);
    REQUIRE(plan.to_convert[0].source == temp_dir.path / "src/Track.wav");
    REQUIRE(plan.skipped.size() == 1);
    REQUIRE(note_for(plan.skipped, temp_dir.path / "src/track.aiff") == "same output as Track.wav");
}

TEST_CASE("scan never lets an output overwrite another source file with or without --overwrite") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/track.wav", "x");
    write_bytes(temp_dir.path / "src/track.flac", "x");
    write_bytes(temp_dir.path / "src/Other.flac", "x");
    write_bytes(temp_dir.path / "src/other.aiff", "x");
    for (bool overwrite : {false, true}) {
        auto options = make_options(temp_dir.path / "src", temp_dir.path / "src"); options.encode.format = Format::Flac; options.overwrite = overwrite;
        string error_message;
        Plan plan = scan(options, error_message);
        REQUIRE(plan.to_convert.empty());
        REQUIRE(plan.skipped.size() == 4);
        REQUIRE(note_for(plan.skipped, temp_dir.path / "src/track.flac") == "output would be the source file");
        REQUIRE(note_for(plan.skipped, temp_dir.path / "src/track.wav") == "output would overwrite a source file");
        REQUIRE(note_for(plan.skipped, temp_dir.path / "src/Other.flac") == "output would be the source file");
        REQUIRE(note_for(plan.skipped, temp_dir.path / "src/other.aiff") == "output would overwrite a source file");
    }
}

// A destination that reaches the source folder by another path (here a symlink) defeats a purely
// lexical comparison of paths; the existing file's identity still gives it away.
TEST_CASE("scan never lets an output overwrite a source reached through a symlinked destination") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/track.wav", "x");
    write_bytes(temp_dir.path / "src/track.flac", "x");
    error_code fs_error;
    fs::create_directory_symlink(temp_dir.path / "src", temp_dir.path / "alias", fs_error);
    if (fs_error) SKIP("cannot create a directory symlink here: " + fs_error.message());
    auto options = make_options(temp_dir.path / "src", temp_dir.path / "alias"); options.encode.format = Format::Flac; options.overwrite = true;
    string error_message;
    Plan plan = scan(options, error_message);
    REQUIRE(plan.to_convert.empty());
    REQUIRE(note_for(plan.skipped, temp_dir.path / "src/track.flac") == "output would be the source file");
    REQUIRE(note_for(plan.skipped, temp_dir.path / "src/track.wav") == "output would overwrite a source file");
}

TEST_CASE("scan does not descend into a destination that lives inside the source") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/one.wav", "x");
    write_bytes(temp_dir.path / "src/out/old.flac", "x");
    auto options = make_options(temp_dir.path / "src", temp_dir.path / "src/out"); options.encode.format = Format::Flac;
    string error_message;
    Plan plan = scan(options, error_message);
    REQUIRE(plan.to_convert.size() == 1);
    REQUIRE(plan.ignored == 0);
}

TEST_CASE("scan reports a missing source") {
    TempDir temp_dir;
    string error_message;
    scan(make_options(temp_dir.path / "nope", temp_dir.path / "out"), error_message);
    REQUIRE_FALSE(error_message.empty());
}
