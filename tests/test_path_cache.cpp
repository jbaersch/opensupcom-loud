#include <catch2/catch_test_macros.hpp>

#include "map/heightmap.hpp"
#include "map/pathfinder.hpp"
#include "map/pathfinding_grid.hpp"

#include <bit>
#include <cstddef>
#include <new>
#include <vector>

namespace {
using namespace osc;
using namespace osc::map;

struct RouteGrid {
    Heightmap height;
    PathfindingGrid grid;
    RouteGrid(bool water = false, f32 wall_length = 90)
        : height(128, 128, 0.25f, std::vector<u16>(129 * 129, water ? 48 : 64)),
          grid(height, 16, water, 1) {
        grid.mark_obstacle(64.5f, 64, 1, wall_length);
    }
};

void same_path(const PathResult& a, const PathResult& b) {
    REQUIRE(a.found == b.found);
    REQUIRE(a.partial == b.partial);
    REQUIRE(a.throttled == b.throttled);
    REQUIRE(a.waypoints.size() == b.waypoints.size());
    for (size_t i = 0; i < a.waypoints.size(); ++i) {
        CHECK(std::bit_cast<u32>(a.waypoints[i].x) == std::bit_cast<u32>(b.waypoints[i].x));
        CHECK(std::bit_cast<u32>(a.waypoints[i].y) == std::bit_cast<u32>(b.waypoints[i].y));
        CHECK(std::bit_cast<u32>(a.waypoints[i].z) == std::bit_cast<u32>(b.waypoints[i].z));
    }
}

PathResult compare_fresh(Pathfinder& warm, const PathfindingGrid& grid, f32 sx, f32 sz, f32 gx,
                         f32 gz, const std::string& layer, f32 draft = 0, bool amphibious = false) {
    Pathfinder fresh(grid);
    warm.reset_request_count();
    const auto expected = fresh.find_path(sx, sz, gx, gz, layer, draft, amphibious);
    const auto actual = warm.find_path(sx, sz, gx, gz, layer, draft, amphibious);
    same_path(actual, expected);
    CHECK(warm.last_nodes_explored() == fresh.last_nodes_explored());
    CHECK(warm.requests_this_tick() == fresh.requests_this_tick());
    return actual;
}
} // namespace

TEST_CASE("Repeated obstructed routes preserve request slots and original search effort",
          "[pathfinding][cache]") {
    RouteGrid world;
    Pathfinder paths(world.grid);
    const auto first = paths.find_path(20.5f, 64.5f, 108.25f, 64.75f, "Land");
    REQUIRE(first.found);
    REQUIRE_FALSE(first.partial);
    const auto original_nodes = paths.last_nodes_explored();
    REQUIRE(original_nodes > 0);
    REQUIRE(paths.requests_this_tick() == 1);
    for (int i = 1; i < Pathfinder::MAX_REQUESTS_PER_TICK; ++i) {
        same_path(paths.find_path(20.5f, 64.5f, 108.25f, 64.75f, "Land"), first);
        CHECK(paths.last_nodes_explored() == original_nodes);
        CHECK(paths.requests_this_tick() == i + 1);
    }
    const auto deferred = paths.find_path(20.5f, 64.5f, 108.25f, 64.75f, "Land");
    CHECK(deferred.throttled);
    CHECK_FALSE(deferred.found);
    CHECK(paths.last_nodes_explored() == 0);
    paths.reset_request_count();
    same_path(paths.find_path(20.5f, 64.5f, 108.25f, 64.75f, "Land"), first);
    CHECK(paths.requests_this_tick() == 1);
}

TEST_CASE("Raw route reuse retains each request's precise world endpoints",
          "[pathfinding][cache]") {
    RouteGrid world;
    Pathfinder paths(world.grid);
    for (f32 offset : {0.125f, 0.25f, 0.625f, 0.875f}) {
        const auto result = compare_fresh(paths, world.grid, 20 + offset, 64 + offset, 108 + offset,
                                          64 + offset, "Land");
        REQUIRE(result.found);
        REQUIRE_FALSE(result.partial);
        REQUIRE(result.waypoints.size() >= 2);
        CHECK(result.waypoints.front().x == 20.5f);
        CHECK(result.waypoints.front().z == 64.5f);
        CHECK(result.waypoints.back().x == 108 + offset);
        CHECK(result.waypoints.back().z == 64 + offset);
    }
}

TEST_CASE("Raw routes follow real obstacle and terrain changes including covered depth",
          "[pathfinding][cache]") {
    RouteGrid world(true);
    Pathfinder paths(world.grid);
    const auto query = [&] {
        for (f32 draft : {-0.0f, 0.0f, 3.25f, 3.5f, 3.5001f, 4.0f, 4.25f})
            for (const auto& layer : {"Water", "Seabed", "Sub", "Land"}) {
                compare_fresh(paths, world.grid, 20.5f, 64.5f, 108.25f, 64.75f, layer, draft);
                compare_fresh(paths, world.grid, 20.5f, 64.5f, 108.25f, 64.75f, layer, draft, true);
            }
    };
    query();
    const auto initial_version = world.grid.version();
    world.grid.mark_obstacle(64.5f, 64, 1, 90);
    REQUIRE(world.grid.version() == initial_version); // refcount only
    query();
    world.grid.clear_obstacle(64.5f, 64, 1, 90);
    REQUIRE(world.grid.version() == initial_version); // still covered
    // Changed terrain includes cells under the wall: depth/base changes
    // invalidate even where the visible footprint still blocks the cell.
    world.height.set_rect(64, 40, 66, 90, 12.5f);
    world.grid.refresh_terrain(world.height, 64, 40, 66, 90);
    REQUIRE(world.grid.version() > initial_version);
    query();
    world.grid.clear_obstacle(64.5f, 64, 1, 90);
    query();
    // A new shallow band changes which exact drafts can cross the map.
    world.height.set_rect(40, 0, 43, 129, 12.5f);
    world.grid.refresh_terrain(world.height, 40, 0, 43, 129);
    query();
    const auto fits =
        compare_fresh(paths, world.grid, 20.5f, 64.5f, 108.25f, 64.75f, "Water", 3.5f);
    REQUIRE(fits.found);
    CHECK_FALSE(fits.partial);
    const auto too_deep =
        compare_fresh(paths, world.grid, 20.5f, 64.5f, 108.25f, 64.75f, "Water", 3.5001f);
    REQUIRE(too_deep.found);
    CHECK(too_deep.partial);
}

TEST_CASE("Raw route state is private across live and reconstructed pathfinders",
          "[pathfinding][cache]") {
    RouteGrid first(false, 90);
    RouteGrid second(false, 30);
    Pathfinder a(first.grid), b(second.grid);
    for (int i = 0; i < 4; ++i) {
        compare_fresh(a, first.grid, 20.5f, 64.5f, 108.25f, 64.75f, "Land");
        compare_fresh(b, second.grid, 20.5f, 64.5f, 108.25f, 64.75f, "Land");
    }
    alignas(Pathfinder) std::byte storage[sizeof(Pathfinder)];
    for (int i = 0; i < 6; ++i) {
        const auto& grid = i % 2 ? first.grid : second.grid;
        auto* reused = new (storage) Pathfinder(grid);
        compare_fresh(*reused, grid, 20.5f, 64.5f, 108.25f, 64.75f, "Land");
        reused->~Pathfinder();
    }
}

TEST_CASE("Route replacement preserves results after many distinct requests and boundaries",
          "[pathfinding][cache]") {
    RouteGrid world;
    Pathfinder paths(world.grid);
    const auto original = compare_fresh(paths, world.grid, 20.5f, 64.5f, 108.25f, 64.75f, "Land");
    // More independently obstructed inputs than the small derived cache can
    // retain. No saved or caller-visible state depends on replacement order.
    for (int i = 0; i < 300; ++i) {
        compare_fresh(paths, world.grid, 10.5f + static_cast<f32>(i % 40),
                      24.5f + static_cast<f32>(i / 40), 108.25f, 64.75f, "Land");
    }
    same_path(compare_fresh(paths, world.grid, 20.5f, 64.5f, 108.25f, 64.75f, "Land"), original);
    for (const auto& layer : {"Land", "Air"})
        for (f32 edge : {-10.0f, 0.0f, 127.875f, 150.0f})
            compare_fresh(paths, world.grid, edge, edge, 128 - edge, 128 - edge, layer);
}
