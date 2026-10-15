#include <catch2/catch_test_macros.hpp>

#include "map/heightmap.hpp"
#include "map/pathfinder.hpp"
#include "map/pathfinding_grid.hpp"

#include <vector>

namespace {

osc::map::Heightmap plain() {
    return {128, 128, 1.0f / 128.0f, std::vector<osc::u16>(129 * 129, 1280)};
}

} // namespace

TEST_CASE("Clear movement segments do not wait behind the expensive search budget",
          "[nav][direct-path]") {
    const auto terrain = plain();
    osc::map::PathfindingGrid grid(terrain, 0, false);
    osc::map::Pathfinder paths(grid);
    // One large order: these routes need only a bounded segment walk,
    // without an A* search or a staggered start over many sim ticks.
    for (int i = 0; i < 128; ++i) {
        CAPTURE(i);
        const auto path = paths.find_path(11, 11, 111, 11, "Land");
        REQUIRE(path.found);
        CHECK_FALSE(path.throttled);
        CHECK_FALSE(path.partial);
        REQUIRE(path.waypoints.size() == 2);
        CHECK(path.waypoints.front().x == 11);
        CHECK(path.waypoints.back().x == 111);
        CHECK(paths.last_nodes_explored() == 0);
    }
    CHECK(paths.requests_this_tick() == 0);
}

TEST_CASE("A direct segment respects collision corners and retains the A star budget",
          "[nav][direct-path]") {
    const auto terrain = plain();
    osc::map::PathfindingGrid grid(terrain, 0, false);
    osc::map::Pathfinder paths(grid);
    grid.mark_obstacle(19, 17, 2, 2);
    grid.mark_obstacle(17, 19, 2, 2);
    for (int i = 0; i < osc::map::Pathfinder::MAX_REQUESTS_PER_TICK; ++i)
        paths.increment_request_count();
    const auto corner = paths.find_path(17, 17, 29, 29, "Land");
    CHECK_FALSE(corner.found);
    CHECK(corner.throttled);
    const auto clear = paths.find_path(17, 25, 111, 25, "Land");
    REQUIRE(clear.found);
    CHECK_FALSE(clear.throttled);
    CHECK(paths.requests_this_tick() == osc::map::Pathfinder::MAX_REQUESTS_PER_TICK);
}

TEST_CASE("Direct movement applies water depth and amphibious passability", "[nav][direct-path]") {
    const auto terrain = plain();
    osc::map::PathfindingGrid grid(terrain, 12, true);
    osc::map::Pathfinder paths(grid);
    for (int i = 0; i < osc::map::Pathfinder::MAX_REQUESTS_PER_TICK; ++i)
        paths.increment_request_count();
    const auto shallow = paths.find_path(11, 11, 111, 11, "Water", 3);
    CHECK(shallow.throttled);
    const auto amphibious = paths.find_path(11, 11, 111, 11, "Land", 0, true);
    REQUIRE(amphibious.found);
    CHECK_FALSE(amphibious.throttled);
    const auto ship = paths.find_path(11, 11, 111, 11, "Water", 1);
    REQUIRE(ship.found);
    CHECK_FALSE(ship.throttled);
    CHECK(paths.requests_this_tick() == osc::map::Pathfinder::MAX_REQUESTS_PER_TICK);
}
