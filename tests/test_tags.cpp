#include <catch2/catch_test_macros.hpp>
#include "core/tags.hpp"
#include "core/options.hpp"

using namespace beatdown;

TEST_CASE("tags_from_filename splits on the first ' - '") {
    Tags tags = tags_from_filename("simple fact - slipz Mastered_Master");
    REQUIRE(tags.artist == "simple fact");
    REQUIRE(tags.title == "slipz Mastered_Master");
}

TEST_CASE("tags_from_filename keeps later separators in the title") {
    Tags tags = tags_from_filename("A - B - C");
    REQUIRE(tags.artist == "A");
    REQUIRE(tags.title == "B - C");
}

TEST_CASE("tags_from_filename without a separator gives only a title") {
    Tags tags = tags_from_filename("untitled export");
    REQUIRE_FALSE(tags.artist.has_value());
    REQUIRE(tags.title == "untitled export");
}

TEST_CASE("tags_from_filename preserves casing and inner whitespace exactly") {
    Tags tags = tags_from_filename("DJ  Name - Track  (Original Mix)");
    REQUIRE(tags.artist == "DJ  Name");
    REQUIRE(tags.title == "Track  (Original Mix)");
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
    Tags primary; primary.title = "T";
    Tags fallback; fallback.title = "ignored"; fallback.artist = "Art";
    Tags merged = merge_tags(primary, fallback);
    REQUIRE(merged.title == "T");
    REQUIRE(merged.artist == "Art");
}

TEST_CASE("Tags::empty is true only when every field is unset") {
    Tags tags;
    REQUIRE(tags.empty());
    tags.genre = "House";
    REQUIRE_FALSE(tags.empty());
}

TEST_CASE("Options::effective_jobs defaults to hardware concurrency and honours --jobs") {
    Options options;
    REQUIRE(options.effective_jobs() >= 1);
    options.jobs = 3;
    REQUIRE(options.effective_jobs() == 3);
}

TEST_CASE("Options::output_extension follows the format") {
    Options options;
    REQUIRE(string(options.output_extension()) == ".mp3");
    options.encode.format = Format::Flac;
    REQUIRE(string(options.output_extension()) == ".flac");
}
