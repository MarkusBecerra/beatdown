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

static Job job(const fs::path& src, const fs::path& out) { Job j; j.source = src; j.output = out; j.source_bytes = static_cast<int64_t>(fs::file_size(src)); return j; }
static Options opts(const fs::path& dst) { Options o; o.destination = dst; return o; }
static bool has_temp_files(const fs::path& dir) {
    for (auto& e : fs::directory_iterator(dir)) if (e.path().filename().string().find(".part") != std::string::npos) return true;
    return false;
}

// ".beatdown-<name>." + 8 hex digits + ".part"
static bool is_temp_name_for(const std::string& temp, const std::string& name) {
    const std::string prefix = ".beatdown-" + name + ".", suffix = ".part";
    if (temp.size() != prefix.size() + 8 + suffix.size()) return false;
    if (!temp.starts_with(prefix) || !temp.ends_with(suffix)) return false;
    for (size_t i = prefix.size(); i < prefix.size() + 8; ++i)
        if (!std::isxdigit(static_cast<unsigned char>(temp[i]))) return false;
    return true;
}

TEST_CASE("temp_path_for is a unique dotfile in the output's folder with a .part suffix") {
    fs::path a = temp_path_for("/x/y/Track.mp3"), b = temp_path_for("/x/y/Track.mp3");
    REQUIRE(a.parent_path() == fs::path("/x/y"));
    REQUIRE(is_temp_name_for(path_to_utf8(a.filename()), "Track.mp3"));
    REQUIRE(is_temp_name_for(path_to_utf8(b.filename()), "Track.mp3"));
    REQUIRE(a != b);
}

TEST_CASE("temp_path_for keeps a non-ASCII filename through UTF-8, not the ANSI codepage") {
    fs::path p = fs::path("/x/y") / path_from_utf8("Måns – 東京.mp3");
    REQUIRE(is_temp_name_for(path_to_utf8(temp_path_for(p).filename()), "Måns – 東京.mp3"));
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
    TempDir t;
    auto src = make_audio(t.path / "src/a.wav", {.seconds = 0.5});
    fs::create_directories(t.path / "out");
    auto stamp = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
    fs::last_write_time(src, stamp);
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(src, t.path / "out/a.mp3"), opts(t.path / "out"), cancel);
    REQUIRE(r.error == "");
    REQUIRE(r.outcome == Outcome::Converted);
    REQUIRE(fs::exists(t.path / "out/a.mp3"));
    REQUIRE(r.output_bytes == static_cast<int64_t>(fs::file_size(t.path / "out/a.mp3")));
    REQUIRE_FALSE(has_temp_files(t.path / "out"));
    // Compared as a millisecond count (not `REQUIRE(a == b)` on the time_points directly):
    // this platform's fs::file_time_type has an __int128 duration rep, which Catch2's
    // generic chrono stringifier can't format (ambiguous operator<<) — so any macro that
    // decomposes the time_points themselves fails to compile, pass or fail. A 2 s tolerance
    // also covers filesystems that round last_write_time to whole seconds.
    auto mtime_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        fs::last_write_time(t.path / "out/a.mp3") - fs::last_write_time(src)).count();
    REQUIRE(mtime_diff_ms < 2000);
    REQUIRE(mtime_diff_ms > -2000);
    REQUIRE(fs::exists(src));
}

TEST_CASE("convert_one fails a corrupt source with a reason and leaves nothing behind") {
    TempDir t;
    write_bytes(t.path / "src/bad.wav", kCorruptWav);
    fs::create_directories(t.path / "out");
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(t.path / "src/bad.wav", t.path / "out/bad.mp3"), opts(t.path / "out"), cancel);
    REQUIRE(r.outcome == Outcome::Failed);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE_FALSE(fs::exists(t.path / "out/bad.mp3"));
    REQUIRE_FALSE(has_temp_files(t.path / "out"));
}

TEST_CASE("convert_one cancelled leaves no output or temp file") {
    TempDir t;
    auto src = make_audio(t.path / "src/a.wav", {.seconds = 5.0});
    fs::create_directories(t.path / "out");
    std::atomic<bool> cancel{true};
    FileResult r = convert_one(job(src, t.path / "out/a.mp3"), opts(t.path / "out"), cancel);
    REQUIRE(r.outcome == Outcome::Cancelled);
    REQUIRE_FALSE(fs::exists(t.path / "out/a.mp3"));
    REQUIRE_FALSE(has_temp_files(t.path / "out"));
}

TEST_CASE("convert_one creates missing sub-folders of the output and replaces an existing output when overwriting") {
    TempDir t;
    auto src = make_audio(t.path / "src/a.wav", {.seconds = 0.2});
    write_bytes(t.path / "out/sub/a.mp3", "old");
    std::atomic<bool> cancel{false};
    Options o = opts(t.path / "out"); o.overwrite = true;
    FileResult r = convert_one(job(src, t.path / "out/sub/a.mp3"), o, cancel);
    REQUIRE(r.outcome == Outcome::Converted);
    REQUIRE(fs::file_size(t.path / "out/sub/a.mp3") > 3);
}

TEST_CASE("resolve_tags prefers source tags and falls back to the filename when asked") {
    TempDir t;
    std::string err;
    auto untagged = Decoder::open(make_audio(t.path / "simple fact - slipz Mastered_Master.wav", {.seconds = 0.1}), err);
    Options o;
    REQUIRE(resolve_tags(*untagged, o).empty());
    o.tag_from_name = true; o.strip_suffixes = {" Mastered_Master"};
    Tags d = resolve_tags(*untagged, o);
    REQUIRE(d.artist == "simple fact");
    REQUIRE(d.title == "slipz");
    Tags src; src.title = "Real Title";
    auto tagged = Decoder::open(make_audio(t.path / "A - B.wav", {.seconds = 0.1, .tags = src}), err);
    Tags m = resolve_tags(*tagged, o);
    REQUIRE(m.title == "Real Title");
    REQUIRE(m.artist == "A");
}

TEST_CASE("convert_one writes the derived tags into the MP3") {
    TempDir t;
    auto src = make_audio(t.path / "src/simple fact - slipz Mastered_Master.wav", {.seconds = 0.2});
    fs::create_directories(t.path / "out");
    Options o = opts(t.path / "out"); o.tag_from_name = true; o.strip_suffixes = {" Mastered_Master"};
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(src, t.path / "out/simple fact - slipz Mastered_Master.mp3"), o, cancel);
    REQUIRE(r.outcome == Outcome::Converted);
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(r.job.output, info, err));
    REQUIRE(info.tags.artist == "simple fact");
    REQUIRE(info.tags.title == "slipz");
}

// A WAV whose raw INFO title is Windows-1252 ("Beyonc\xE9 Mix"), as an older Windows tool writes it.
static fs::path cp1252_titled_wav(const fs::path& file) {
    Tags raw; raw.title = std::string("Beyonc\xE9 Mix");
    return make_audio(file, {.seconds = 0.2, .tags = raw});
}

TEST_CASE("convert_one writes a Windows-1252 INFO title into the MP3 as proper text") {
    TempDir t;
    auto src = cp1252_titled_wav(t.path / "src/a.wav");
    fs::create_directories(t.path / "out");
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(src, t.path / "out/a.mp3"), opts(t.path / "out"), cancel);
    REQUIRE(r.outcome == Outcome::Converted);
    Mp3Info info; std::string err;
    REQUIRE(parse_mp3(t.path / "out/a.mp3", info, err));
    REQUIRE(info.tags.title == "Beyoncé Mix");
}

TEST_CASE("convert_one writes a Windows-1252 INFO title into the FLAC as proper text without crashing") {
    TempDir t;
    auto src = cp1252_titled_wav(t.path / "src/a.wav");
    fs::create_directories(t.path / "out");
    Options o = opts(t.path / "out"); o.encode.format = Format::Flac;
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(src, t.path / "out/a.flac"), o, cancel);
    REQUIRE(r.outcome == Outcome::Converted);
    std::string err;
    auto d = Decoder::open(t.path / "out/a.flac", err);
    REQUIRE(d);
    REQUIRE(d->tags().title == "Beyoncé Mix");
}

// A WAV whose raw INFO title contains U+FFFF, a noncharacter libFLAC's vorbis-comment validation
// rejects outright; libsndfile 1.2.2 ignores that rejection and appends an uninitialised comment
// entry instead of failing cleanly, which crashes the process (SIGSEGV) and loses the whole batch.
// sanitize_utf8 must replace it (and its sibling U+FFFE) with U+FFFD before either encoder sees it.
TEST_CASE("convert_one replaces the FFFE/FFFF noncharacters libFLAC rejects instead of crashing") {
    TempDir t;
    Tags raw; raw.title = std::string("Mix \xEF\xBF\xBF");
    auto src = make_audio(t.path / "src/a.wav", {.seconds = 0.2, .tags = raw});
    fs::create_directories(t.path / "out");
    Options o = opts(t.path / "out"); o.encode.format = Format::Flac;
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(src, t.path / "out/a.flac"), o, cancel);
    REQUIRE(r.outcome == Outcome::Converted);
    std::string err;
    auto d = Decoder::open(t.path / "out/a.flac", err);
    REQUIRE(d);
    REQUIRE(d->tags().title == "Mix \xEF\xBF\xBD");
}

TEST_CASE("convert_one produces FLAC when asked") {
    TempDir t;
    auto src = make_audio(t.path / "src/a.wav", {.seconds = 0.2});
    fs::create_directories(t.path / "out");
    Options o = opts(t.path / "out"); o.encode.format = Format::Flac;
    std::atomic<bool> cancel{false};
    FileResult r = convert_one(job(src, t.path / "out/a.flac"), o, cancel);
    REQUIRE(r.outcome == Outcome::Converted);
    REQUIRE(fs::exists(t.path / "out/a.flac"));
}
