#include <catch2/catch_test_macros.hpp>
#include "core/id3v2.hpp"
#include "core/decoder.hpp"
#include "fixtures.hpp"

using namespace beatdown;

static Tags sample() {
    Tags t; t.title = "Café Track"; t.artist = "Artist"; t.album = "Album"; t.date = "2026"; t.track = "3"; t.genre = "House"; t.comment = "hi";
    return t;
}

TEST_CASE("parse_id3v2 reads UTF-8, UTF-16 and latin1 text frames") {
    for (int enc : {0, 1, 3}) {
        std::string bytes = build_id3v2_for_test(sample(), enc);
        Tags t; size_t size = 0;
        REQUIRE(parse_id3v2(bytes, t, &size));
        REQUIRE(size == bytes.size());
        REQUIRE(t.title == "Café Track");
        REQUIRE(t.artist == "Artist");
        REQUIRE(t.album == "Album");
        REQUIRE(t.date == "2026");
        REQUIRE(t.track == "3");
        REQUIRE(t.genre == "House");
        REQUIRE(t.comment == "hi");
    }
}

TEST_CASE("parse_id3v2 rejects non-tags and truncated tags") {
    Tags t;
    REQUIRE_FALSE(parse_id3v2("RIFF....", t));
    std::string bytes = build_id3v2_for_test(sample(), 3);
    REQUIRE_FALSE(parse_id3v2(std::string_view(bytes).substr(0, 20), t));
}

TEST_CASE("read_wav_id3_chunk finds an id3 chunk appended to a WAV") {
    TempDir t;
    auto f = make_audio(t.path / "a.wav", {.subtype = SF_FORMAT_PCM_16, .seconds = 0.05});
    std::string wav = read_file(f);
    std::string tag = build_id3v2_for_test(sample(), 3);
    std::string chunk = "id3 ";
    uint32_t n = static_cast<uint32_t>(tag.size());
    chunk.append(reinterpret_cast<const char*>(&n), 4);   // little-endian on every target we build
    chunk += tag;
    if (tag.size() % 2) chunk.push_back('\0');
    uint32_t riff = static_cast<uint32_t>(wav.size() + chunk.size() - 8);
    wav.replace(4, 4, reinterpret_cast<const char*>(&riff), 4);
    write_bytes(t.path / "b.wav", wav + chunk);

    Tags got = read_wav_id3_chunk(t.path / "b.wav");
    REQUIRE(got.title == "Café Track");
    REQUIRE(read_wav_id3_chunk(f).empty());

    std::string err;
    auto d = Decoder::open(t.path / "b.wav", err);
    REQUIRE(d);
    REQUIRE(d->tags().artist == "Artist");
}

TEST_CASE("parse_id3v2 validates footer when v2.4 footer flag is set") {
    // v2.4 tag with footer flag set and footer appended
    std::string bytes = build_id3v2_for_test(sample(), 3);
    bytes[3] = 4;  // change major version to 4
    bytes[5] |= 0x10;  // set footer flag
    // Append 10-byte footer: "3DI" + 7 header bytes (version, flags, size)
    bytes += "3DI";
    bytes.push_back(4); bytes.push_back(0); bytes.push_back(0);  // version 4.0
    bytes.push_back(bytes[5]); bytes.push_back(bytes[6]); bytes.push_back(bytes[7]); bytes.push_back(bytes[8]);  // size bytes

    Tags t;
    size_t tag_size = 0;
    REQUIRE(parse_id3v2(bytes, t, &tag_size));
    REQUIRE(tag_size == bytes.size());
}

TEST_CASE("parse_id3v2 rejects v2.4 footer flag without footer bytes") {
    // v2.4 tag with footer flag set but no footer appended
    std::string bytes = build_id3v2_for_test(sample(), 3);
    bytes[3] = 4;  // change major version to 4
    bytes[5] |= 0x10;  // set footer flag (but don't append footer)

    Tags t;
    REQUIRE_FALSE(parse_id3v2(bytes, t));
}
