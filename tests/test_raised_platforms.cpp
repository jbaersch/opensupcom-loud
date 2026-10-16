#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "map/heightmap.hpp"
#include "map/terrain.hpp"
#include "sim/manipulator.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/unit_command.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lua.h>
}

#include <memory>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;
using osc::f32;
using osc::sim::CollisionShape;
using osc::sim::CollisionShapeType;
using osc::sim::CommandType;
using osc::sim::SimState;
using osc::sim::Unit;
using osc::sim::UnitCommand;

namespace {

struct LuaGuard {
    lua_State* L = lua_open();
    ~LuaGuard() { lua_close(L); }
};

constexpr f32 kGround = 1000.0f / 128.0f;

void flat(SimState& sim) {
    std::vector<osc::u16> heights(129 * 129, 1000);
    osc::map::Heightmap hm(128, 128, 1.0f / 128.0f, std::move(heights));
    sim.set_terrain(std::make_unique<osc::map::Terrain>(std::move(hm), 0.0f, false));
    sim.build_pathfinding_grid();
    sim.add_army("ARMY_1", "ARMY_1");
    sim.set_fog_of_war("none");
    sim.set_victory_condition("sandbox");
}

// UEB0101's deck, then a ramp rising 1 along x beside it.
std::vector<f32> decks() {
    return {-1.1f, -1.6f, 0.5f, 1.1f, -1.6f, 0.5f, -1.1f, 1.9f, 0.5f, 1.1f, 1.9f, 0.5f,
            1.1f,  -1.6f, 0.0f, 2.1f, -1.6f, 1.0f, 1.1f,  1.9f, 0.0f, 2.1f, 1.9f, 1.0f};
}

Unit* factory(SimState& sim, f32 x, f32 z) {
    auto u = std::make_unique<Unit>();
    u->set_army(0);
    u->set_position({x, kGround, z});
    u->set_max_health(100.0f);
    u->set_health(100.0f);
    u->set_size_xz(4.2f, 4.4f);
    u->set_size_y(0.6f);
    CollisionShape box;
    box.type = CollisionShapeType::BOX;
    box.cy = 0.3f;
    box.sx = 2.1f;
    box.sy = 0.3f;
    box.sz = 2.2f;
    u->set_default_collision_shape(box);
    u->set_raised_platforms(decks());
    auto* raw = u.get();
    sim.entity_registry().register_entity(std::move(u));
    return raw;
}

Unit* tank(SimState& sim, f32 x, f32 z) {
    auto u = std::make_unique<Unit>();
    u->set_army(0);
    u->set_max_speed(1.0f);
    u->set_motion_type("RULEUMT_Land");
    u->set_position({x, kGround, z});
    u->set_max_health(100.0f);
    u->set_health(100.0f);
    u->set_size_xz(0.6f, 0.8f);
    u->set_size_y(0.4f);
    Unit::Drive drive;
    drive.max_accel = 50.0f;
    u->set_drive(drive);
    auto* raw = u.get();
    sim.entity_registry().register_entity(std::move(u));
    return raw;
}

} // namespace

TEST_CASE("A land unit on a factory's deck stands on its raised platform", "[platforms]") {
    LuaGuard g;
    SimState sim(g.L, nullptr);
    flat(sim);
    factory(sim, 64.0f, 64.0f);
    Unit* u = tank(sim, 64.0f, 64.5f);
    const auto* terrain = sim.terrain();
    CHECK(u->ground_y(terrain, 64.0f, 64.5f) == Approx(kGround + 0.5f));
    CHECK(u->ground_y(terrain, 65.6f, 64.5f) == Approx(kGround + 0.5f));
    CHECK(u->ground_y(terrain, 64.0f, 70.0f) == kGround);
    Unit* away = tank(sim, 64.0f, 72.0f);
    CHECK(away->ground_y(terrain, 64.0f, 64.5f) == kGround);
}

TEST_CASE("A unit driving off a deck rides it, and a dying factory's deck is gone", "[platforms]") {
    LuaGuard g;
    SimState sim(g.L, nullptr);
    flat(sim);
    Unit* f = factory(sim, 64.0f, 64.0f);
    Unit* u = tank(sim, 64.0f, 64.0f);
    UnitCommand move;
    move.type = CommandType::Move;
    move.target_pos = {64.0f, 0.0f, 80.0f};
    u->push_command(move, true);
    sim.tick();
    sim.tick();
    REQUIRE(u->position().z > 64.0f);
    REQUIRE(u->position().z < 65.5f);
    CHECK(u->position().y == Approx(kGround + 0.5f));
    f->begin_dying();
    sim.tick();
    CHECK(u->position().y == kGround);
}

TEST_CASE("Filtered collider queries retain the original candidates", "[platforms][spatial]") {
    osc::sim::EntityRegistry registry;
    for (int i = 0; i < 80; ++i) {
        auto unit = std::make_unique<Unit>();
        unit->set_position({static_cast<f32>((i * 13) % 128), 8, static_cast<f32>((i * 37) % 128)});
        unit->set_army(i % 3);
        CollisionShape box;
        box.type = i % 7 == 0 ? CollisionShapeType::NONE : CollisionShapeType::BOX;
        box.sx = i % 5 == 0 ? 16.0f : 1.0f;
        box.sy = 1;
        box.sz = 2;
        unit->set_default_collision_shape(box);
        if (i % 11 == 0) unit->set_orientation({0, 2, 0, 1});
        registry.register_entity(std::move(unit));
    }
    const auto accept = [](const osc::sim::Entity& e) { return e.army() == 1; };
    const auto check = [&] {
        for (f32 lo : {-10.0f, 0.0f, 32.0f, 64.0f, 120.0f}) {
            std::vector<osc::u32> before, after;
            registry.any_unit_collider(lo, lo, lo + 20, lo + 20, [&](const auto& e) {
                if (accept(e)) before.push_back(e.entity_id());
                return false;
            });
            registry.any_unit_collider_if(lo + 20, lo + 20, lo, lo, accept, [&](const auto& e) {
                after.push_back(e.entity_id());
                return false;
            });
            std::sort(before.begin(), before.end());
            std::sort(after.begin(), after.end());
            CHECK(before == after); // including duplicate large-collider visits
            const auto yes = [](const auto&) { return true; };
            CHECK(registry.any_unit_collider_if(lo, lo, lo + 20, lo + 20, accept, yes) ==
                  !before.empty());
        }
    };
    check(); // fallback
    registry.init_spatial_grid(128, 128);
    check();
    registry.find(2)->set_position({32, 8, 32});
    registry.find(5)->mark_destroyed();
    registry.unregister_entity(8);
    registry.collect_garbage();
    registry.init_spatial_grid(256, 256);
    check();
}

TEST_CASE("Ground stance sees platforms added and removed among ordinary units", "[platforms]") {
    LuaGuard g;
    SimState sim(g.L, nullptr);
    flat(sim);
    for (int i = 0; i < 100; ++i) tank(sim, 63 + static_cast<f32>(i % 3), 64);
    Unit* f = factory(sim, 64, 64);
    Unit* u = tank(sim, 64, 64);
    const auto ground = [&] { return u->ground_y(sim.terrain(), 64, 64); };
    CHECK(ground() == Approx(kGround + 0.5f));
    f->set_raised_platforms({});
    CHECK(ground() == kGround);
    f->set_raised_platforms(decks());
    CHECK(ground() == Approx(kGround + 0.5f));
    f->set_position({100, kGround, 100});
    CHECK(ground() == kGround);
    f->set_position({64, kGround, 64});
    CHECK(ground() == Approx(kGround + 0.5f));
    sim.entity_registry().unregister_entity(f->entity_id());
    CHECK(ground() == kGround);
}
