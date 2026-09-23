#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "core/scheduler.hpp"

using namespace beatdown;
using namespace std::chrono_literals;

TEST_CASE("run_parallel runs every job exactly once") {
    std::vector<std::atomic<int>> hits(50);
    std::atomic<bool> cancel{false};
    size_t started = run_parallel(4, hits.size(), [&](size_t i) { hits[i]++; }, cancel);
    REQUIRE(started == 50);
    for (auto& h : hits) REQUIRE(h == 1);
}

TEST_CASE("run_parallel with one worker preserves order") {
    std::vector<size_t> order;
    std::atomic<bool> cancel{false};
    run_parallel(1, 10, [&](size_t i) { order.push_back(i); }, cancel);
    for (size_t i = 0; i < 10; ++i) REQUIRE(order[i] == i);
}

TEST_CASE("run_parallel actually overlaps work") {
    std::atomic<int> active{0}, peak{0};
    std::atomic<bool> cancel{false};
    run_parallel(4, 8, [&](size_t) {
        int a = ++active;
        int p = peak.load();
        while (a > p && !peak.compare_exchange_weak(p, a)) {}
        std::this_thread::sleep_for(50ms);
        --active;
    }, cancel);
    REQUIRE(peak >= 2);
}

TEST_CASE("run_parallel stops launching once cancelled") {
    std::atomic<bool> cancel{false};
    std::atomic<int> ran{0};
    size_t started = run_parallel(2, 100, [&](size_t i) {
        ran++;
        if (i == 0) cancel = true;
        std::this_thread::sleep_for(20ms);
    }, cancel);
    REQUIRE(started < 100);
    REQUIRE(ran == static_cast<int>(started));
}

TEST_CASE("run_parallel with zero jobs returns immediately") {
    std::atomic<bool> cancel{false};
    REQUIRE(run_parallel(4, 0, [](size_t) {}, cancel) == 0);
}
