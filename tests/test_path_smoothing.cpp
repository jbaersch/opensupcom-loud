#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "map/heightmap.hpp"
#include "map/pathfinder.hpp"
#include "map/pathfinding_grid.hpp"

#include <vector>

namespace {

osc::map::Heightmap flat_map() {
    return {32, 32, 1.0f / 128.0f, std::vector<osc::u16>(33 * 33, 1280)};
}

} // namespace

TEST_CASE("Smoothed movement paths retain the clear way out of a blocked diagonal corner",
          "[nav][path-smoothing]") {
    const int sx = GENERATE(-1, 1);
    const int sz = GENERATE(-1, 1);
    CAPTURE(sx, sz);
    const auto terrain = flat_map();
    osc::map::PathfindingGrid grid(terrain, 0, false);
    // Start at a cell centre. The two cardinal neighbours towards the
    // destination are buildings; a valid path must first leave backwards
    // along at least one axis, even though the diagonal cells are clear.
    constexpr float start = 17;
    grid.mark_obstacle(start + 2 * sx, start, 2, 2);
    grid.mark_obstacle(start, start + 2 * sz, 2, 2);
    osc::map::Pathfinder paths(grid);
    const auto path = paths.find_path(start, start, start + 12 * sx, start + 12 * sz, "Land");
    REQUIRE(path.found);
    REQUIRE_FALSE(path.partial);
    REQUIRE(path.waypoints.size() >= 2);
    const auto& departure = path.waypoints[1];
    CHECK((sx * (departure.x - start) < 0 || sz * (departure.z - start) < 0));
}

TEST_CASE("A smoothed movement segment never crosses an obstacle between raster sample cells",
          "[nav][path-smoothing]") {
    const bool water = GENERATE(false, true);
    CAPTURE(water);
    const auto terrain = flat_map();
    osc::map::PathfindingGrid grid(terrain, 20, water);
    grid.mark_obstacle(19, 19, 2, 2);
    osc::map::Pathfinder paths(grid);
    const std::string layer = water ? "Water" : "Land";
    const auto path = paths.find_path(17, 17, 29, 23, layer);
    REQUIRE(path.found);
    REQUIRE_FALSE(path.partial);
    REQUIRE(path.waypoints.size() >= 2);
    for (size_t segment = 1; segment < path.waypoints.size(); ++segment) {
        const auto& from = path.waypoints[segment - 1];
        const auto& to = path.waypoints[segment];
        for (int sample = 0; sample <= 128; ++sample) {
            const float t = sample / 128.0f;
            const float x = from.x + (to.x - from.x) * t;
            const float z = from.z + (to.z - from.z) * t;
            // The building occupies the open interior of [18,20]^2.
            // Test physical space, independently of the smoothing walk.
            CAPTURE(segment, sample, x, z);
            CHECK_FALSE((x > 18 && x < 20 && z > 18 && z < 20));
        }
    }
}
