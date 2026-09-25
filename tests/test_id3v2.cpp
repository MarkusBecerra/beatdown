#include <catch2/catch_test_macros.hpp>
#include "core/id3v2.hpp"
#include "core/decoder.hpp"
#include "fixtures.hpp"

using namespace beatdown;

static Tags sample() {
    Tags tags; tags.title = "Café Track"; tags.artist = "Artist"; tags.album = "Album"; tags.date = "2026"; tags.track = "3"; tags.genre = "House"; tags.comment = "hi";
    return tags;
}

TEST_CASE("parse_id3v2 reads UTF-8, UTF-16 and latin1 text frames") {
    for (int encoding : {0, 1, 3}) {
        string bytes = build_id3v2_for_test(sample(), encoding);
        Tags tags; size_t size = 0;
        REQUIRE(parse_id3v2(bytes, tags, &size));
        REQUIRE(size == bytes.size());
        REQUIRE(tags.title == "Café Track");
        REQUIRE(tags.artist == "Artist");
        REQUIRE(tags.album == "Album");
        REQUIRE(tags.date == "2026");
        REQUIRE(tags.track == "3");
        REQUIRE(tags.genre == "House");
        REQUIRE(tags.comment == "hi");
    }
}

TEST_CASE("parse_id3v2 re-decodes a UTF-8 (encoding 3) frame that isn't valid UTF-8 as Windows-1252") {
    Tags raw; raw.title = string("Beyonc\xE9 Mix");
    Tags tags;
    REQUIRE(parse_id3v2(build_id3v2_for_test(raw, 3), tags));
    REQUIRE(tags.title == "Beyoncé Mix");
}

TEST_CASE("parse_id3v2 rejects non-tags and truncated tags") {
    Tags tags;
    REQUIRE_FALSE(parse_id3v2("RIFF....", tags));
    string bytes = build_id3v2_for_test(sample(), 3);
    REQUIRE_FALSE(parse_id3v2(string_view(bytes).substr(0, 20), tags));
}

TEST_CASE("read_wav_id3_chunk finds an id3 chunk appended to a WAV") {
    TempDir temp_dir;
    auto file = make_audio(temp_dir.path / "a.wav", {.subtype = SF_FORMAT_PCM_16, .seconds = 0.05});
    string wav = read_file(file);
    string tag = build_id3v2_for_test(sample(), 3);
    string chunk = "id3 ";
    uint32_t chunk_size = static_cast<uint32_t>(tag.size());
    chunk.append(reinterpret_cast<const char*>(&chunk_size), 4);   // little-endian on every target we build
    chunk += tag;
    if (tag.size() % 2) chunk.push_back('\0');
    uint32_t riff = static_cast<uint32_t>(wav.size() + chunk.size() - 8);
    wav.replace(4, 4, reinterpret_cast<const char*>(&riff), 4);
    write_bytes(temp_dir.path / "b.wav", wav + chunk);

    Tags got = read_wav_id3_chunk(temp_dir.path / "b.wav");
    REQUIRE(got.title == "Café Track");
    REQUIRE(read_wav_id3_chunk(file).empty());

    string error_message;
    auto decoder = Decoder::open(temp_dir.path / "b.wav", error_message);
    REQUIRE(decoder);
    REQUIRE(decoder->tags().artist == "Artist");
}

TEST_CASE("parse_id3v2 validates footer when v2.4 footer flag is set") {
    // v2.4 tag with footer flag set and footer appended
    string bytes = build_id3v2_for_test(sample(), 3);
    bytes[3] = 4;  // change major version to 4
    bytes[5] |= 0x10;  // set footer flag
    // Append 10-byte footer: "3DI" + 7 header bytes (version, flags, size)
    bytes += "3DI";
    bytes.push_back(4); bytes.push_back(0); bytes.push_back(0);  // version 4.0
    bytes.push_back(bytes[5]); bytes.push_back(bytes[6]); bytes.push_back(bytes[7]); bytes.push_back(bytes[8]);  // size bytes

    Tags tags;
    size_t tag_size = 0;
    REQUIRE(parse_id3v2(bytes, tags, &tag_size));
    REQUIRE(tag_size == bytes.size());
}

TEST_CASE("parse_id3v2 rejects v2.4 footer flag without footer bytes") {
    // v2.4 tag with footer flag set but no footer appended
    string bytes = build_id3v2_for_test(sample(), 3);
    bytes[3] = 4;  // change major version to 4
    bytes[5] |= 0x10;  // set footer flag (but don't append footer)

    Tags tags;
    REQUIRE_FALSE(parse_id3v2(bytes, tags));
}
