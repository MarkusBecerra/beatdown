#include <catch2/catch_test_macros.hpp>
#include <string>
#include "core/version.hpp"

TEST_CASE("version string is the project version") {
    REQUIRE(std::string(beatdown::version()) == "0.1.0");
}
