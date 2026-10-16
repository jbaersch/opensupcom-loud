#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/fixed_step.hpp"
#include "core/game_state.hpp"

#include <vector>

using Catch::Matchers::WithinAbs;

TEST_CASE("A live frame yields between expensive ticks without interrupting the first",
          "[fixedstep][tick-budget]") {
    double elapsed = 0;
    int calls = 0;
    const int completed = osc::run_budgeted_ticks(
        8, 0.016,
        [&] {
            ++calls;
            elapsed += 0.100;
            return true;
        },
        [&] { return elapsed; });
    CHECK(completed == 1);
    CHECK(calls == 1);
    CHECK_THAT(elapsed, WithinAbs(0.100, 1e-12));
    // Even a zero budget must not prevent a due tick making progress.
    CHECK(osc::run_budgeted_ticks(1, 0.0, [] { return true; }, [] { return 1.0; }) == 1);
}

TEST_CASE("Cheap ticks may share a frame but never exceed its scheduled count",
          "[fixedstep][tick-budget]") {
    double elapsed = 0;
    CHECK(osc::run_budgeted_ticks(
              8, 0.016,
              [&] {
                  elapsed += 0.007;
                  return true;
              },
              [&] { return elapsed; }) == 3);
    CHECK_THAT(elapsed, WithinAbs(0.021, 1e-12)); // The final tick is indivisible.
    CHECK(osc::run_budgeted_ticks(8, 0.016, [] { return true; }, [] { return 0.0; }) == 8);
    int calls = 0;
    CHECK(osc::run_budgeted_ticks(
              0, 0.016,
              [&] {
                  ++calls;
                  return true;
              },
              [] { return 0.0; }) == 0);
    CHECK(calls == 0);
}

TEST_CASE("The soft budget keeps the consumed fractional time and drops unstarted catchup time",
          "[fixedstep][tick-budget]") {
    double accumulator = 0.02;
    bool one_tick = false;
    SECTION("Ordinary overdue ticks") {
        const int scheduled = osc::session_ticks(accumulator, one_tick, 0.37, 0.1, 8);
        REQUIRE(scheduled == 3);
        CHECK(osc::run_budgeted_ticks(
                  scheduled, 0.016, [] { return true; }, [] { return 0.1; }) == 1);
        CHECK_THAT(accumulator, WithinAbs(0.09, 1e-12));
        CHECK(osc::session_ticks(accumulator, one_tick, 0.011, 0.1, 8) == 1);
        CHECK_THAT(accumulator, WithinAbs(0.001, 1e-12));
    }
    SECTION("The existing eight-tick backlog cap also remains in force") {
        const int scheduled = osc::session_ticks(accumulator, one_tick, 2.5, 0.1, 8);
        REQUIRE(scheduled == 8);
        const double fraction = accumulator;
        CHECK(osc::run_budgeted_ticks(
                  scheduled, 0.016, [] { return true; }, [] { return 0.1; }) == 1);
        CHECK(accumulator == fraction);
        CHECK_THAT(accumulator, WithinAbs(0.0999, 1e-12));
        CHECK(osc::session_ticks(accumulator, one_tick, 0.0002, 0.1, 8) == 1);
        CHECK_THAT(accumulator, WithinAbs(0.0001, 1e-12));
    }
}

TEST_CASE("Paused-beat resume and the requested host rate retain their clock contracts",
          "[fixedstep][tick-budget]") {
    double accumulator = 0.07;
    bool one_tick = true;
    osc::GameStateManager game;
    game.set_sim_rate(10);
    const int scheduled = osc::session_ticks(accumulator, one_tick, 0.35 * game.speed(), 0.1, 8);
    REQUIRE(scheduled == 1);
    CHECK(osc::run_budgeted_ticks(scheduled, 0.016, [] { return true; }, [] { return 0.1; }) == 1);
    CHECK_FALSE(one_tick);
    CHECK(accumulator == 0);
    CHECK(game.sim_rate() == 10);
    CHECK(game.speed() == 10);
    CHECK(osc::session_ticks(accumulator, one_tick, 0.005 * game.speed(), 0.1, 8) == 0);
    CHECK_THAT(accumulator, WithinAbs(0.05, 1e-12));
}

TEST_CASE("Yielding preserves every native tick and its ordered work, and stops at replay end",
          "[fixedstep][tick-budget]") {
    int native_tick = 0;
    std::vector<int> executed;
    for (int frame = 0; frame < 12; ++frame) {
        CHECK(osc::run_budgeted_ticks(
                  8, 0.016,
                  [&] {
                      executed.push_back(++native_tick);
                      return true;
                  },
                  [] { return 0.100; }) == 1);
    }
    CHECK(executed == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    int remaining = 3;
    CHECK(osc::run_budgeted_ticks(
              8, 0.016, [&] { return remaining-- > 0; }, [] { return 0.0; }) == 3);
}
