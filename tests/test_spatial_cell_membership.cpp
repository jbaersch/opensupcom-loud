#include <catch2/catch_test_macros.hpp>

#include "sim/entity_registry.hpp"
#include "lua/lua_state.hpp"
#include "map/heightmap.hpp"
#include "map/terrain.hpp"
#include "sim/sim_state.hpp"
#include "sim/sim_snapshot.hpp"
#include "sim/manipulator.hpp"
#include "sim/prop.hpp"
#include "sim/projectile.hpp"
#include "sim/unit.hpp"

extern "C" {
#include <lua.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <limits>
#include <vector>

namespace {
using namespace osc::sim;
using osc::u32;

void populate(EntityRegistry& r) {
    // Adjacent float values straddle both 16- and 32-unit cell boundaries;
    // negatives and positions past the map exercise clamped edge cells.
    const std::array<float, 12> coordinates{-2,
                                            0,
                                            std::nextafter(16.0f, 0.0f),
                                            16,
                                            std::nextafter(16.0f, 32.0f),
                                            std::nextafter(32.0f, 0.0f),
                                            32,
                                            std::nextafter(32.0f, 64.0f),
                                            64,
                                            78,
                                            129,
                                            150};
    for (unsigned i = 0; i < coordinates.size() * 3; ++i) {
        std::unique_ptr<Entity> e;
        if (i % 3 == 0) e = std::make_unique<Unit>();
        else if (i % 3 == 1) e = std::make_unique<Prop>();
        else e = std::make_unique<Projectile>();
        e->set_position({coordinates[i % coordinates.size()], 0,
                         coordinates[(i * 5 + 1) % coordinates.size()]});
        CollisionShape shape;
        shape.type = i % 5 == 0 ? CollisionShapeType::NONE : CollisionShapeType::SPHERE;
        shape.sx = i % 7 == 0 ? 30.0f : 1.0f;
        e->set_default_collision_shape(shape);
        r.register_entity(std::move(e));
    }
}

std::vector<u32> unit_ids(const EntityRegistry& r, float x, float z, float radius) {
    std::vector<u32> result;
    for (const Entity* e : r.units_in_radius(x, z, radius)) result.push_back(e->entity_id());
    return result;
}

void compare(const EntityRegistry& indexed, const EntityRegistry& flat) {
    for (float x : {-3.0f, 0.0f, 16.0f, 32.0f, 80.0f, 129.0f, 152.0f})
        for (float radius : {0.0f, 0.25f, 8.0f, 16.0f, 35.0f, 200.0f}) {
            INFO("x=" << x << " radius=" << radius);
            const float z = 32;
            CHECK(indexed.collect_in_radius(x, z, radius) == flat.collect_in_radius(x, z, radius));
            CHECK(indexed.collect_in_rect(x - radius, z - radius, x + radius, z + radius) ==
                  flat.collect_in_rect(x - radius, z - radius, x + radius, z + radius));
            CHECK(unit_ids(indexed, x, z, radius) == unit_ids(flat, x, z, radius));
            std::vector<u32> visits;
            indexed.visit_units_in_radius(x, z, radius,
                                          [&](Entity& e) { visits.push_back(e.entity_id()); });
            std::sort(visits.begin(), visits.end());
            CHECK(visits == unit_ids(flat, x, z, radius));
            std::vector<u32> a, b;
            indexed.collect_colliders(x - radius, z - radius, x + radius, z + radius, a);
            flat.collect_colliders(x - radius, z - radius, x + radius, z + radius, b);
            CHECK(a == b);
        }
}
} // namespace

TEST_CASE("Spatial acceleration preserves exact query membership across cell and lifecycle changes",
          "[registry][spatial-membership]") {
    EntityRegistry flat, indexed;
    indexed.init_spatial_grid(130, 79);
    populate(flat);
    populate(indexed);
    for (int step = 0; step < 8; ++step) {
        INFO("step=" << step);
        compare(indexed, flat);
        for (EntityRegistry* r : {&flat, &indexed}) {
            // Reverse movement order forces noncanonical arrival order within a cell.
            for (u32 id = 36; id > 0; --id)
                if (Entity* e = r->find(id)) {
                    auto p = e->position();
                    p.x += (step % 2 ? 1.0f : -1.0f) * 16.0f;
                    e->set_position(p);
                }
            if (step == 0) r->find(2)->mark_destroyed();
            if (step == 1) r->unregister_entity(4);
            if (step == 2) {
                CollisionShape shape;
                shape.type = CollisionShapeType::SPHERE;
                shape.sx = 30;
                r->find(5)->set_collision_shape(shape);
            }
            if (step == 3) r->find(5)->set_collision_shape(CollisionShape{});
            if (step == 4) r->collect_garbage();
        }
        if (step == 5) indexed.init_spatial_grid(256, 128);
    }
}

TEST_CASE("Restored spatial queries rebuild their acceleration from saved positions",
          "[registry][spatial-membership][savegame]") {
    osc::lua::LuaState lua_a;
    osc::lua::LuaState lua_b;
    SimState original(lua_a.raw(), nullptr);
    SimState restored(lua_b.raw(), nullptr);
    const auto setup = [](SimState& sim) {
        osc::map::Heightmap heightmap(128, 128, 0.25f, std::vector<osc::u16>(129 * 129, 64));
        sim.set_terrain(std::make_unique<osc::map::Terrain>(std::move(heightmap), 0, false));
        sim.build_pathfinding_grid();
        sim.entity_registry().init_spatial_grid(128, 128);
        sim.add_army("ARMY_1", "ARMY_1");
        sim.set_victory_condition("sandbox");
    };
    setup(original);
    setup(restored);
    populate(original.entity_registry());
    // Destination boot positions differ from the saved world's; a stale
    // cell index must not survive the registry's load reconstruction.
    populate(restored.entity_registry());
    original.entity_registry().find(1)->set_position({33, 0, 31});
    original.entity_registry().find(2)->set_position({15, 0, 17});
    original.entity_registry().advance_collision_cells();
    // LuaState leaves its initialization value on the stack; snapshots
    // require the main thread at rest, as at the simulation tick boundary.
    lua_settop(lua_a.raw(), 0);
    lua_settop(lua_b.raw(), 0);
    std::vector<osc::u8> bytes;
    const auto saved = save_snapshot(original, bytes);
    INFO(saved);
    REQUIRE(saved.empty());
    const auto loaded = load_snapshot(restored, bytes);
    INFO(loaded);
    REQUIRE(loaded.empty());
    CHECK(restored.compute_sync_checksum() == original.compute_sync_checksum());
    compare(restored.entity_registry(), original.entity_registry());
    // Subsequent movement exercises removal from reconstructed cells too.
    for (SimState* sim : {&original, &restored})
        sim->entity_registry().find(1)->set_position({65, 0, 47});
    compare(restored.entity_registry(), original.entity_registry());
}

TEST_CASE("Radius grids retain every entity kind accepted by rounded float distances",
          "[registry][spatial-membership][radius-rounding]") {
    struct Boundary {
        float center;
        float radius;
        float position;
    };
    // The first two cases straddle different acceleration cell sizes. The
    // large-center case loses half a unit in subtraction, much more than
    // one ULP of the naively computed lower endpoint (32).
    for (const Boundary boundary : {
             Boundary{32, 16, std::nextafter(16.0f, 0.0f)},
             Boundary{64, 32, std::nextafter(32.0f, 0.0f)},
             Boundary{16777248, 16777216, 31.5f},
             Boundary{32, 32, -0.000001f},
             Boundary{-32, 16, std::nextafter(-16.0f, 0.0f)},
             Boundary{0, 0, 1.0e-23f},
             Boundary{0, -0.0f, -1.0e-23f},
             Boundary{0, 1.0e-30f, 1.0e-23f},
             Boundary{64, -32, 48},
         }) {
        INFO("center=" << boundary.center << " radius=" << boundary.radius
                       << " position=" << boundary.position);
        EntityRegistry flat, indexed;
        indexed.init_spatial_grid(256, 256);
        for (EntityRegistry* registry : {&flat, &indexed}) {
            auto unit = std::make_unique<Unit>();
            unit->set_position({boundary.position, 0, 64});
            registry->register_entity(std::move(unit));
            auto prop = std::make_unique<Prop>();
            prop->set_position({boundary.position, 0, 64});
            registry->register_entity(std::move(prop));
            auto projectile = std::make_unique<Projectile>();
            projectile->set_position({boundary.position, 0, 64});
            registry->register_entity(std::move(projectile));
        }
        const float dx = boundary.position - boundary.center;
        REQUIRE(dx * dx <= boundary.radius * boundary.radius);
        const auto check = [&](float center) {
            const auto expected = flat.collect_in_radius(center, 64, boundary.radius);
            CHECK(expected == std::vector<u32>{1, 2, 3});
            CHECK(indexed.collect_in_radius(center, 64, boundary.radius) == expected);
            CHECK(unit_ids(indexed, center, 64, boundary.radius) == std::vector<u32>{1});
            std::vector<u32> visited;
            indexed.visit_units_in_radius(center, 64, boundary.radius, [&](Entity& entity) {
                visited.push_back(entity.entity_id());
            });
            CHECK(visited == std::vector<u32>{1});
        };
        check(boundary.center);
        // Relinking must not change the rounded-distance membership either.
        // Tiny underflow examples would round away under this translation.
        if (std::abs(boundary.position) > 1.0e-20f && boundary.radius >= 1) {
            for (EntityRegistry* registry : {&flat, &indexed})
                registry->for_each([](Entity& entity) {
                    auto position = entity.position();
                    position.x += 64;
                    entity.set_position(position);
                });
            check(boundary.center + 64);
        }
    }
}

TEST_CASE("Radius queries use the exact float predicate for extreme query inputs",
          "[registry][spatial-membership][radius-rounding]") {
    EntityRegistry flat, indexed;
    indexed.init_spatial_grid(256, 256);
    populate(flat);
    populate(indexed);
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float center : {0.0f, 64.0f, std::numeric_limits<float>::max(),
                               -std::numeric_limits<float>::max(), infinity, -infinity, nan})
        for (const float radius :
             {0.0f, -0.0f, 1.0e-30f, -1.0f, std::sqrt(std::numeric_limits<float>::max()), 2.0e20f,
              infinity, -infinity, nan}) {
            INFO("center=" << center << " radius=" << radius);
            CHECK(indexed.collect_in_radius(center, 64, radius) ==
                  flat.collect_in_radius(center, 64, radius));
            const auto expected = unit_ids(flat, center, 64, radius);
            CHECK(unit_ids(indexed, center, 64, radius) == expected);
            std::vector<u32> visited;
            indexed.visit_units_in_radius(
                center, 64, radius, [&](Entity& entity) { visited.push_back(entity.entity_id()); });
            std::sort(visited.begin(), visited.end());
            CHECK(visited == expected);
        }
}
