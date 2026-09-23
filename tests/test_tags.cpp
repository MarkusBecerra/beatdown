#include <catch2/catch_test_macros.hpp>
#include "core/tags.hpp"
#include "core/options.hpp"

using namespace beatdown;

TEST_CASE("tags_from_filename splits on the first ' - '") {
    Tags t = tags_from_filename("simple fact - slipz Mastered_Master");
    REQUIRE(t.artist == "simple fact");
    REQUIRE(t.title == "slipz Mastered_Master");
}

TEST_CASE("tags_from_filename keeps later separators in the title") {
    Tags t = tags_from_filename("A - B - C");
    REQUIRE(t.artist == "A");
    REQUIRE(t.title == "B - C");
}

TEST_CASE("tags_from_filename without a separator gives only a title") {
    Tags t = tags_from_filename("untitled export");
    REQUIRE_FALSE(t.artist.has_value());
    REQUIRE(t.title == "untitled export");
}

TEST_CASE("tags_from_filename preserves casing and inner whitespace exactly") {
    Tags t = tags_from_filename("DJ  Name - Track  (Original Mix)");
    REQUIRE(t.artist == "DJ  Name");
    REQUIRE(t.title == "Track  (Original Mix)");
}

TEST_CASE("strip_suffixes removes each matching trailing suffix once, in order") {
    REQUIRE(strip_suffixes("slipz Mastered_Master", {" Mastered_Master"}) == "slipz");
    REQUIRE(strip_suffixes("slipz", {" Mastered_Master"}) == "slipz");
    REQUIRE(strip_suffixes("x_v2_final", {"_final", "_v2"}) == "x");
    REQUIRE(strip_suffixes("x_final_v2", {"_final", "_v2"}) == "x_final");
}

TEST_CASE("strip_suffixes never empties a title") {
    REQUIRE(strip_suffixes("_final", {"_final"}) == "_final");
}

TEST_CASE("merge_tags fills only missing fields from the fallback") {
    Tags a; a.title = "T";
    Tags b; b.title = "ignored"; b.artist = "Art";
    Tags m = merge_tags(a, b);
    REQUIRE(m.title == "T");
    REQUIRE(m.artist == "Art");
}

TEST_CASE("Tags::empty is true only when every field is unset") {
    Tags t;
    REQUIRE(t.empty());
    t.genre = "House";
    REQUIRE_FALSE(t.empty());
}

TEST_CASE("Options::effective_jobs defaults to hardware concurrency and honours --jobs") {
    Options o;
    REQUIRE(o.effective_jobs() >= 1);
    o.jobs = 3;
    REQUIRE(o.effective_jobs() == 3);
}

TEST_CASE("Options::output_extension follows the format") {
    Options o;
    REQUIRE(std::string(o.output_extension()) == ".mp3");
    o.encode.format = Format::Flac;
    REQUIRE(std::string(o.output_extension()) == ".flac");
}
