#include <catch2/catch_test_macros.hpp>

#include "sim/entity_registry.hpp"
#include "sim/manipulator.hpp"
#include "sim/projectile.hpp"
#include "sim/prop.hpp"
#include "sim/unit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {

using osc::u32;
using namespace osc::sim;

Unit& add_unit(EntityRegistry& registry, float x, float z) {
    auto unit = std::make_unique<Unit>();
    unit->set_position({x, 0, z});
    auto* result = unit.get();
    registry.register_entity(std::move(unit));
    return *result;
}

std::vector<u32> visited_ids(const EntityRegistry& registry, float x, float z, float radius) {
    std::vector<u32> ids;
    registry.visit_units_in_radius(x, z, radius,
                                   [&](Entity& entity) { ids.push_back(entity.entity_id()); });
    std::sort(ids.begin(), ids.end());
    // A reduction must see each candidate exactly once, even after relinking.
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    return ids;
}

void check_ordered_query(const EntityRegistry& registry, float x, float z, float radius) {
    std::vector<u32> ordered;
    for (const Entity* unit : registry.units_in_radius(x, z, radius))
        ordered.push_back(unit->entity_id());
    CHECK(visited_ids(registry, x, z, radius) == ordered);
}

} // namespace

TEST_CASE("Unordered unit visits preserve radius membership through entity lifecycle changes",
          "[registry][unit-radius-visit]") {
    EntityRegistry registry;
    bool grid = false;
    SECTION("Without a grid") {}
    SECTION("With a grid") {
        grid = true;
        registry.init_spatial_grid(256, 256);
    }
    std::vector<u32> units;
    units.reserve(90);
    for (int i = 0; i < 90; ++i)
        units.push_back(add_unit(registry, static_cast<float>((i * 43) % 296 - 20),
                                 static_cast<float>((i * 29) % 296 - 20))
                            .entity_id());
    // Other entity types share the broad grid but never the unit visit.
    for (int i = 0; i < 20; ++i) {
        auto prop = std::make_unique<Prop>();
        prop->set_position({128, 0, 128});
        registry.register_entity(std::move(prop));
        auto projectile = std::make_unique<Projectile>();
        projectile->set_position({128, 0, 128});
        registry.register_entity(std::move(projectile));
    }
    for (int step = 0; step < 12; ++step) {
        INFO("grid " << grid << " step " << step);
        // Reverse arrival order within cells as well as crossing cell borders.
        for (auto it = units.rbegin(); it != units.rend(); ++it) {
            if (auto* unit = registry.find(*it)) {
                auto at = unit->position();
                at.x += (step % 2 ? 1.0f : -1.0f) * 35;
                unit->set_position(at);
            }
        }
        if (step == 2) registry.find(units[5])->mark_destroyed();
        if (step == 3) registry.unregister_entity(units[9]);
        if (step == 4) static_cast<Unit*>(registry.find(units[13]))->begin_dying();
        if (step == 5) {
            registry.collect_garbage();
            add_unit(registry, 128, 128);
        }
        if (step == 6 && grid) registry.init_spatial_grid(512, 512);
        for (const auto at : {Vector3{0, 0, 0}, Vector3{128, 0, 128}, Vector3{256, 0, 256}})
            for (const float radius : {0.0f, 1.0f, 32.0f, 65.0f, 512.0f})
                check_ordered_query(registry, at.x, at.z, radius);
    }
}

TEST_CASE("Unit visits keep exact inclusive boundaries and the existing dying-unit contract",
          "[registry][unit-radius-visit]") {
    EntityRegistry registry;
    SECTION("Without a grid") {}
    SECTION("Across a grid boundary") {
        registry.init_spatial_grid(128, 128);
    }
    auto& center = add_unit(registry, 32, 32);
    auto& boundary = add_unit(registry, 42, 32);
    auto& outside = add_unit(registry, std::nextafter(42.0f, 100.0f), 32);
    auto& dying = add_unit(registry, 32, 35);
    dying.begin_dying();
    auto& destroyed = add_unit(registry, 32, 36);
    destroyed.mark_destroyed();
    auto& departed = add_unit(registry, 32, 37);
    registry.unregister_entity(departed.entity_id());
    CHECK(visited_ids(registry, 32, 32, 10) ==
          std::vector<u32>{center.entity_id(), boundary.entity_id(), dying.entity_id()});
    CHECK(visited_ids(registry, 32, 32, 0) == std::vector<u32>{center.entity_id()});
    CHECK(visited_ids(registry, 32, 32, -0.0f) == std::vector<u32>{center.entity_id()});
    CHECK(outside.position().x > 42.0f);
}

TEST_CASE("Ungridded unit visits preserve non-comparable and overflowing distance behavior",
          "[registry][unit-radius-visit]") {
    EntityRegistry registry;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    auto& center = add_unit(registry, 0, 0);
    add_unit(registry, nan, 0);
    auto& infinite = add_unit(registry, infinity, 0);
    auto& huge = add_unit(registry, 2.0e20f, 0);
    CHECK(visited_ids(registry, 0, 0, 1) == std::vector<u32>{center.entity_id()});
    // Infinite squared distance compares equal to the overflowed radius;
    // NaN never passes the existing <= comparison.
    CHECK(visited_ids(registry, 0, 0, 3.0e20f) ==
          std::vector<u32>{center.entity_id(), infinite.entity_id(), huge.entity_id()});
    check_ordered_query(registry, 0, 0, 3.0e20f);
}
