#include <catch2/catch_test_macros.hpp>
#include "core/platform/platform.hpp"
#include "fixtures.hpp"

using namespace beatdown;

TEST_CASE("file_id agrees for two paths naming the same file and differs for two different files") {
    TempDir t;
    write_bytes(t.path / "a.txt", "hello");
    write_bytes(t.path / "b.txt", "hello");   // same content as a.txt; identity must still differ

    auto id_a = platform::file_id(t.path / "a.txt");
    auto id_b = platform::file_id(t.path / "b.txt");
    REQUIRE(id_a);
    REQUIRE(id_b);
    REQUIRE(id_a != id_b);

    // "dir/./file" names the same file as "dir/file" on every OS without needing any extra setup
    // (every directory has a "." entry), so this branch always runs; the symlink branch is tried
    // first since it's closer to the real cases this exists for (NFC/NFD twins, a symlinked
    // destination), but needs a privilege this environment might not have (e.g. Windows without
    // Developer Mode or elevation).
    std::error_code ec;
    fs::create_symlink(t.path / "a.txt", t.path / "a-link.txt", ec);
    if (!ec) REQUIRE(platform::file_id(t.path / "a-link.txt") == id_a);
    REQUIRE(platform::file_id(t.path / "." / "a.txt") == id_a);
}

TEST_CASE("file_id returns nullopt for a path that doesn't exist") {
    TempDir t;
    REQUIRE_FALSE(platform::file_id(t.path / "nope.txt"));
}
