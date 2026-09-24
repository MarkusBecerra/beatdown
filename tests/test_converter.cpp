#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <atomic>
#include <cctype>
#include "core/converter.hpp"
#include "core/decoder.hpp"
#include "core/mp3_parse.hpp"
#include "core/unicode.hpp"
#include "fixtures.hpp"

using namespace beatdown;
using Catch::Matchers::ContainsSubstring;

static Job job(const fs::path& src, const fs::path& out) { Job result; result.source = src; result.output = out; result.source_bytes = static_cast<int64_t>(fs::file_size(src)); return result; }
static Options make_options(const fs::path& destination) { Options options; options.destination = destination; return options; }
static bool has_temp_files(const fs::path& dir) {
    for (auto& entry : fs::directory_iterator(dir)) if (entry.path().filename().string().find(".part") != string::npos) return true;
    return false;
}

// ".beatdown-<name>." + 8 hex digits + ".part"
static bool is_temp_name_for(const string& temp, const string& name) {
    const string prefix = ".beatdown-" + name + ".", suffix = ".part";
    if (temp.size() != prefix.size() + 8 + suffix.size()) return false;
    if (!temp.starts_with(prefix) || !temp.ends_with(suffix)) return false;
    for (size_t index = prefix.size(); index < prefix.size() + 8; ++index)
        if (!isxdigit(static_cast<unsigned char>(temp[index]))) return false;
    return true;
}

TEST_CASE("temp_path_for is a unique dotfile in the output's folder with a .part suffix") {
    fs::path first = temp_path_for("/x/y/Track.mp3"), second = temp_path_for("/x/y/Track.mp3");
    REQUIRE(first.parent_path() == fs::path("/x/y"));
    REQUIRE(is_temp_name_for(path_to_utf8(first.filename()), "Track.mp3"));
    REQUIRE(is_temp_name_for(path_to_utf8(second.filename()), "Track.mp3"));
    REQUIRE(first != second);
}

TEST_CASE("temp_path_for keeps a non-ASCII filename through UTF-8, not the ANSI codepage") {
    fs::path path = fs::path("/x/y") / path_from_utf8("Måns – 東京.mp3");
    REQUIRE(is_temp_name_for(path_to_utf8(temp_path_for(path).filename()), "Måns – 東京.mp3"));
}

TEST_CASE("looks_like_disk_full flags available space below the estimate") {
    REQUIRE(looks_like_disk_full("some encode error", 100, 1000));
}

TEST_CASE("looks_like_disk_full ignores an unrelated error when space is plentiful") {
    REQUIRE_FALSE(looks_like_disk_full("lame encode error -1", 1'000'000'000, 1000));
}

TEST_CASE("looks_like_disk_full recognizes an out-of-space message even with plentiful space") {
    REQUIRE(looks_like_disk_full("No space left on device", 1'000'000'000, 1000));
}

TEST_CASE("convert_one produces a verified MP3, removes the temp file and copies mtime") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.5});
    fs::create_directories(temp_dir.path / "out");
    auto stamp = fs::file_time_type::clock::now() - chrono::hours(24 * 30);
    fs::last_write_time(src, stamp);
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(result.error == "");
    REQUIRE(result.outcome == Outcome::Converted);
    REQUIRE(fs::exists(temp_dir.path / "out/a.mp3"));
    REQUIRE(result.output_bytes == static_cast<int64_t>(fs::file_size(temp_dir.path / "out/a.mp3")));
    REQUIRE_FALSE(has_temp_files(temp_dir.path / "out"));
    // Compared as a millisecond count (not `REQUIRE(a == b)` on the time_points directly):
    // this platform's fs::file_time_type has an __int128 duration rep, which Catch2's
    // generic chrono stringifier can't format (ambiguous operator<<) — so any macro that
    // decomposes the time_points themselves fails to compile, pass or fail. A 2 s tolerance
    // also covers filesystems that round last_write_time to whole seconds.
    auto mtime_diff_ms = chrono::duration_cast<chrono::milliseconds>(
        fs::last_write_time(temp_dir.path / "out/a.mp3") - fs::last_write_time(src)).count();
    REQUIRE(mtime_diff_ms < 2000);
    REQUIRE(mtime_diff_ms > -2000);
    REQUIRE(fs::exists(src));
}

TEST_CASE("convert_one fails a corrupt source with a reason and leaves nothing behind") {
    TempDir temp_dir;
    write_bytes(temp_dir.path / "src/bad.wav", kCorruptWav);
    fs::create_directories(temp_dir.path / "out");
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(temp_dir.path / "src/bad.wav", temp_dir.path / "out/bad.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(result.outcome == Outcome::Failed);
    REQUIRE_FALSE(result.error.empty());
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out/bad.mp3"));
    REQUIRE_FALSE(has_temp_files(temp_dir.path / "out"));
}

TEST_CASE("convert_one cancelled leaves no output or temp file") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 5.0});
    fs::create_directories(temp_dir.path / "out");
    atomic<bool> cancel{true};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(result.outcome == Outcome::Cancelled);
    REQUIRE_FALSE(fs::exists(temp_dir.path / "out/a.mp3"));
    REQUIRE_FALSE(has_temp_files(temp_dir.path / "out"));
}

TEST_CASE("convert_one creates missing sub-folders of the output and replaces an existing output when overwriting") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.2});
    write_bytes(temp_dir.path / "out/sub/a.mp3", "old");
    atomic<bool> cancel{false};
    Options options = make_options(temp_dir.path / "out"); options.overwrite = true;
    FileResult result = convert_one(job(src, temp_dir.path / "out/sub/a.mp3"), options, cancel);
    REQUIRE(result.outcome == Outcome::Converted);
    REQUIRE(fs::file_size(temp_dir.path / "out/sub/a.mp3") > 3);
}

TEST_CASE("resolve_tags prefers source tags and falls back to the filename when asked") {
    TempDir temp_dir;
    string err;
    auto untagged = Decoder::open(make_audio(temp_dir.path / "simple fact - slipz Mastered_Master.wav", {.seconds = 0.1}), err);
    Options options;
    REQUIRE(resolve_tags(*untagged, options).empty());
    options.tag_from_name = true; options.strip_suffixes = {" Mastered_Master"};
    Tags derived = resolve_tags(*untagged, options);
    REQUIRE(derived.artist == "simple fact");
    REQUIRE(derived.title == "slipz");
    Tags source_tags; source_tags.title = "Real Title";
    auto tagged = Decoder::open(make_audio(temp_dir.path / "A - B.wav", {.seconds = 0.1, .tags = source_tags}), err);
    Tags merged = resolve_tags(*tagged, options);
    REQUIRE(merged.title == "Real Title");
    REQUIRE(merged.artist == "A");
}

TEST_CASE("convert_one writes the derived tags into the MP3") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/simple fact - slipz Mastered_Master.wav", {.seconds = 0.2});
    fs::create_directories(temp_dir.path / "out");
    Options options = make_options(temp_dir.path / "out"); options.tag_from_name = true; options.strip_suffixes = {" Mastered_Master"};
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/simple fact - slipz Mastered_Master.mp3"), options, cancel);
    REQUIRE(result.outcome == Outcome::Converted);
    Mp3Info info; string err;
    REQUIRE(parse_mp3(result.job.output, info, err));
    REQUIRE(info.tags.artist == "simple fact");
    REQUIRE(info.tags.title == "slipz");
}

// A WAV whose raw INFO title is Windows-1252 ("Beyonc\xE9 Mix"), as an older Windows tool writes it.
static fs::path cp1252_titled_wav(const fs::path& file) {
    Tags raw; raw.title = string("Beyonc\xE9 Mix");
    return make_audio(file, {.seconds = 0.2, .tags = raw});
}

TEST_CASE("convert_one writes a Windows-1252 INFO title into the MP3 as proper text") {
    TempDir temp_dir;
    auto src = cp1252_titled_wav(temp_dir.path / "src/a.wav");
    fs::create_directories(temp_dir.path / "out");
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(result.outcome == Outcome::Converted);
    Mp3Info info; string err;
    REQUIRE(parse_mp3(temp_dir.path / "out/a.mp3", info, err));
    REQUIRE(info.tags.title == "Beyoncé Mix");
}

TEST_CASE("convert_one writes a Windows-1252 INFO title into the FLAC as proper text without crashing") {
    TempDir temp_dir;
    auto src = cp1252_titled_wav(temp_dir.path / "src/a.wav");
    fs::create_directories(temp_dir.path / "out");
    Options options = make_options(temp_dir.path / "out"); options.encode.format = Format::Flac;
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.flac"), options, cancel);
    REQUIRE(result.outcome == Outcome::Converted);
    string err;
    auto decoder = Decoder::open(temp_dir.path / "out/a.flac", err);
    REQUIRE(decoder);
    REQUIRE(decoder->tags().title == "Beyoncé Mix");
}

// A WAV whose raw INFO title contains U+FFFF, a noncharacter libFLAC's vorbis-comment validation
// rejects outright; libsndfile 1.2.2 ignores that rejection and appends an uninitialised comment
// entry instead of failing cleanly, which crashes the process (SIGSEGV) and loses the whole batch.
// sanitize_utf8 must replace it (and its sibling U+FFFE) with U+FFFD before either encoder sees it.
TEST_CASE("convert_one replaces the FFFE/FFFF noncharacters libFLAC rejects instead of crashing") {
    TempDir temp_dir;
    Tags raw; raw.title = string("Mix \xEF\xBF\xBF");
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.2, .tags = raw});
    fs::create_directories(temp_dir.path / "out");
    Options options = make_options(temp_dir.path / "out"); options.encode.format = Format::Flac;
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.flac"), options, cancel);
    REQUIRE(result.outcome == Outcome::Converted);
    string err;
    auto decoder = Decoder::open(temp_dir.path / "out/a.flac", err);
    REQUIRE(decoder);
    REQUIRE(decoder->tags().title == "Mix \xEF\xBF\xBD");
}

// Task 18 fix round 2, item 6: verify_mp3's minimum-size check is "at least one audio frame
// after the Info/Xing frame", not a fixed byte count -- a very short, low-bitrate file is
// legitimately small and must not be rejected as "implausibly small".
TEST_CASE("convert_one converts a very short (0.05 s) source at 128 kbps") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.05});
    fs::create_directories(temp_dir.path / "out");
    Options options = make_options(temp_dir.path / "out");
    options.encode.bitrate = 128;
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.mp3"), options, cancel);
    REQUIRE(result.outcome == Outcome::Converted);
}

TEST_CASE("convert_one produces FLAC when asked") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.2});
    fs::create_directories(temp_dir.path / "out");
    Options options = make_options(temp_dir.path / "out"); options.encode.format = Format::Flac;
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.flac"), options, cancel);
    REQUIRE(result.outcome == Outcome::Converted);
    REQUIRE(fs::exists(temp_dir.path / "out/a.flac"));
}

TEST_CASE("convert_one sets peak_dbfs for MP3 outputs and leaves it empty for FLAC") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 0.3});
    fs::create_directories(temp_dir.path / "out");
    atomic<bool> cancel{false};

    FileResult mp3_result = convert_one(job(src, temp_dir.path / "out/a.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(mp3_result.outcome == Outcome::Converted);
    REQUIRE(mp3_result.peak_dbfs.has_value());

    Options flac_options = make_options(temp_dir.path / "out"); flac_options.encode.format = Format::Flac;
    FileResult flac_result = convert_one(job(src, temp_dir.path / "out/a.flac"), flac_options, cancel);
    REQUIRE(flac_result.outcome == Outcome::Converted);
    REQUIRE_FALSE(flac_result.peak_dbfs.has_value());
}

// Task 18 fix round 2, item 3: an MP3 renamed to .wav still decodes as MPEG -- refused outright
// for either output format, since re-encoding lossy audio compounds its losses for no benefit.
TEST_CASE("convert_one refuses an MP3 renamed to .wav, for both output formats") {
    TempDir temp_dir;
    auto real_src = make_audio(temp_dir.path / "real.wav", {.seconds = 0.3});
    fs::create_directories(temp_dir.path / "out");
    atomic<bool> cancel{false};
    FileResult mp3_result = convert_one(job(real_src, temp_dir.path / "out/real.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(mp3_result.outcome == Outcome::Converted);

    auto disguised = temp_dir.path / "src/disguised.wav";
    fs::create_directories(disguised.parent_path());
    fs::copy_file(temp_dir.path / "out/real.mp3", disguised);

    FileResult disguised_mp3_result = convert_one(job(disguised, temp_dir.path / "out2/a.mp3"), make_options(temp_dir.path / "out2"), cancel);
    REQUIRE(disguised_mp3_result.outcome == Outcome::Failed);
    REQUIRE_THAT(disguised_mp3_result.error, ContainsSubstring("source is MP3 data — not re-encoding lossy audio"));

    Options flac_options = make_options(temp_dir.path / "out2"); flac_options.encode.format = Format::Flac;
    FileResult disguised_flac_result = convert_one(job(disguised, temp_dir.path / "out2/a.flac"), flac_options, cancel);
    REQUIRE(disguised_flac_result.outcome == Outcome::Failed);
    REQUIRE_THAT(disguised_flac_result.error, ContainsSubstring("source is MP3 data — not re-encoding lossy audio"));
}

// Task 18 fix round 2, item 4: a source whose header declares more frames than are actually
// readable (e.g. truncated after being written) must not make content_check blame the output --
// the output was correctly encoded from what could actually be read.
TEST_CASE("convert_one succeeds when the source's declared frame count exceeds what's actually readable") {
    TempDir temp_dir;
    auto src = make_audio(temp_dir.path / "src/a.wav", {.seconds = 2.0});
    string data = read_file(src);
    write_bytes(src, data.substr(0, data.size() - 2000));  // well under verify_output's 1 s tolerance
    fs::create_directories(temp_dir.path / "out");
    atomic<bool> cancel{false};
    FileResult result = convert_one(job(src, temp_dir.path / "out/a.mp3"), make_options(temp_dir.path / "out"), cancel);
    REQUIRE(result.outcome == Outcome::Converted);
}
