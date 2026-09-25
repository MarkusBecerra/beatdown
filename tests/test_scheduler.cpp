#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include "core/scheduler.hpp"

using namespace beatdown;
using namespace std::chrono_literals;

TEST_CASE("run_parallel runs every job exactly once") {
    vector<atomic<int>> hits(50);
    atomic<bool> cancel{false};
    size_t started = run_parallel(4, hits.size(), [&](size_t index) { hits[index]++; }, cancel);
    REQUIRE(started == 50);
    for (auto& hit : hits) REQUIRE(hit == 1);
}

TEST_CASE("run_parallel with one worker preserves order") {
    vector<size_t> order;
    atomic<bool> cancel{false};
    run_parallel(1, 10, [&](size_t index) { order.push_back(index); }, cancel);
    for (size_t index = 0; index < 10; ++index) REQUIRE(order[index] == index);
}

TEST_CASE("run_parallel actually overlaps work") {
    atomic<int> active{0}, peak{0};
    atomic<bool> cancel{false};
    run_parallel(4, 8, [&](size_t) {
        int current = ++active;
        int previous_peak = peak.load();
        while (current > previous_peak && !peak.compare_exchange_weak(previous_peak, current)) {}
        sleep_for(50ms);
        --active;
    }, cancel);
    REQUIRE(peak >= 2);
}

TEST_CASE("run_parallel stops launching once cancelled") {
    atomic<bool> cancel{false};
    atomic<int> ran{0};
    size_t started = run_parallel(2, 100, [&](size_t index) {
        ran++;
        if (index == 0) cancel = true;
        sleep_for(20ms);
    }, cancel);
    REQUIRE(started < 100);
    REQUIRE(ran == static_cast<int>(started));
}

TEST_CASE("run_parallel with zero jobs returns immediately") {
    atomic<bool> cancel{false};
    REQUIRE(run_parallel(4, 0, [](size_t) {}, cancel) == 0);
}

TEST_CASE("run_parallel with single worker rethrows first exception and stops launching") {
    atomic<int> ran{0};
    atomic<bool> cancel{false};
    REQUIRE_THROWS_AS(
        run_parallel(1, 10, [&](size_t index) {
            ran++;
            if (index == 3) throw runtime_error("boom");
        }, cancel),
        runtime_error
    );
    REQUIRE(ran == 4);
}

TEST_CASE("run_parallel with multiple workers rethrows first exception") {
    atomic<int> ran{0};
    atomic<bool> cancel{false};
    REQUIRE_THROWS_AS(
        run_parallel(4, 50, [&](size_t index) {
            ran++;
            if (index == 0) throw runtime_error("boom");
            sleep_for(10ms);
        }, cancel),
        runtime_error
    );
    REQUIRE(ran < 50);
}
