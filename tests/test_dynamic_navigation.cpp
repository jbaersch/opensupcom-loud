#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "map/heightmap.hpp"
#include "map/pathfinder.hpp"
#include "map/pathfinding_grid.hpp"
#include "map/terrain.hpp"
#include "sim/manipulator.hpp"
#include "sim/sim_snapshot.hpp"
#include "sim/sim_state.hpp"
#include "sim/state_io.hpp"
#include "sim/unit.hpp"

#include <bit>
#include <memory>
#include <cmath>
#include <vector>

extern "C" {
#include <lua.h>
}

namespace {

struct DynamicWorld {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};

    DynamicWorld() {
        osc::map::Heightmap heightmap(128, 128, 1.0f / 128.0f,
                                      std::vector<osc::u16>(129 * 129, 1280));
        sim.set_terrain(std::make_unique<osc::map::Terrain>(std::move(heightmap), 0, false));
        sim.build_pathfinding_grid();
        sim.add_army("ARMY_1", "Mover");
        sim.set_victory_condition("sandbox");
        osc::sim::GameSetup setup;
        setup.scenario = "/maps/test/test_scenario.lua";
        setup.seed = 17;
        sim.set_game_setup(setup);
    }

    static std::vector<osc::u8> nav_bytes(const osc::sim::Navigator& nav) {
        std::vector<osc::u8> bytes;
        osc::sim::StateWriter writer(bytes);
        osc::sim::StateIO::save(writer, nav);
        return bytes;
    }

    osc::sim::Unit& mover() {
        auto unit = std::make_unique<osc::sim::Unit>();
        unit->set_army(0);
        unit->set_motion_type("RULEUMT_Land");
        unit->set_layer("Land");
        unit->set_position({17, 10, 65});
        unit->set_orientation(osc::sim::euler_to_quat(1.5707964f, 0, 0));
        unit->set_max_health(100);
        unit->set_health(100);
        unit->set_max_speed(8);
        unit->navigator().set_sim_state(&sim);
        osc::sim::Unit::Drive drive;
        drive.max_accel = 80;
        drive.max_brake = 80;
        drive.turn_rate = 3.1415927f;
        drive.rotate_on_spot = true;
        unit->set_drive(drive);
        auto* raw = unit.get();
        sim.entity_registry().register_entity(std::move(unit));
        return *raw;
    }

    osc::sim::Unit& structure(float x, float z, float sx, float sz) {
        auto unit = std::make_unique<osc::sim::Unit>();
        unit->set_army(0);
        unit->set_motion_type("RULEUMT_None");
        unit->add_category("STRUCTURE");
        unit->set_position({x, 10, z});
        unit->set_footprint_size(sx, sz);
        unit->set_max_health(100);
        unit->set_health(100);
        auto* raw = unit.get();
        sim.entity_registry().register_entity(std::move(unit));
        sim.occupy_footprint(*raw);
        return *raw;
    }
};

} // namespace

TEST_CASE("Default movement avoids a structure added after its path was accepted",
          "[nav][dynamic-navigation]") {
    DynamicWorld world;
    auto& unit = world.mover();
    SECTION("A unit that can pivot") {}
    SECTION("A unit with a minimum moving turn and a turn radius") {
        auto drive = unit.drive();
        drive.rotate_on_spot = false;
        drive.turn_radius = 8;
        unit.set_drive(drive);
    }
    osc::sim::UnitCommand move;
    move.type = osc::sim::CommandType::Move;
    move.target_pos = {111, 10, 65};
    unit.push_command(move, true);
    world.sim.tick();
    REQUIRE(unit.navigator().is_moving());
    REQUIRE(unit.position().x < 30);
    // A new ordinary structure closes the already accepted straight path,
    // leaving ample space to go around either end.
    world.structure(65, 65, 8, 24);
    REQUIRE(world.sim.pathfinding_grid()->get(32, 32) == osc::map::CellPassability::Obstacle);
    bool crossed_structure = false;
    bool retained_blocked_turn = false;
    for (int tick = 0; tick < 250; ++tick) {
        const auto before = unit.position();
        const auto heading = osc::sim::quat_yaw(unit.orientation());
        world.sim.tick();
        const auto p = unit.position();
        crossed_structure |= p.x > 61 && p.x < 69 && p.z > 53 && p.z < 77;
        const float turn =
            std::abs(std::remainder(osc::sim::quat_yaw(unit.orientation()) - heading, 6.2831853f));
        if (unit.navigator().status() == osc::sim::Navigator::Status::WaitingForPath &&
            before.x == p.x && before.z == p.z) {
            retained_blocked_turn |= turn > 0;
            // Refusal retains only the turn this drive step computed; it
            // does not grant an instant pivot or increase its turn rate.
            CHECK(turn <= unit.drive().turn_rate * 0.1f + 1e-5f);
        }
    }
    CHECK_FALSE(crossed_structure);
    CHECK(unit.position().x > 108);
    CHECK(unit.command_queue().empty());
    CHECK(retained_blocked_turn);
}

TEST_CASE("Removing a real structure invalidates a previously failed default path request",
          "[nav][dynamic-navigation][path-grid-invalidation]") {
    DynamicWorld world;
    auto& unit = world.mover();
    auto& building = world.structure(96, 96, 84, 84);
    const osc::sim::Vector3 goal{96, 10, 96};
    auto& nav = unit.navigator();
    nav.set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
    REQUIRE(nav.status() == osc::sim::Navigator::Status::Idle);
    REQUIRE(world.sim.pathfinder()->requests_this_tick() == 1);
    world.sim.entity_registry().unregister_entity(building.entity_id());
    world.sim.pathfinder()->reset_request_count();
    nav.set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
    CHECK(nav.is_moving());
    // The now-open route uses the ordinary direct-segment fast path.
    CHECK(world.sim.pathfinder()->requests_this_tick() == 0);
}

TEST_CASE("An unchanged grid still suppresses repeated failed requests",
          "[nav][dynamic-navigation][path-grid-invalidation]") {
    DynamicWorld world;
    auto& unit = world.mover();
    world.structure(96, 96, 84, 84);
    const osc::sim::Vector3 goal{96, 10, 96};
    auto& nav = unit.navigator();
    nav.set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
    REQUIRE(nav.status() == osc::sim::Navigator::Status::Idle);
    // A duplicate covering footprint changes reference counts, but no
    // passability. Neither adding nor removing it should clear the memo.
    auto& overlap = world.structure(96, 96, 84, 84);
    world.sim.entity_registry().unregister_entity(overlap.entity_id());
    for (int retry = 1; retry < osc::sim::Navigator::FAILED_PATH_RETRY_CALLS; ++retry) {
        world.sim.pathfinder()->reset_request_count();
        nav.set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
        REQUIRE(nav.status() == osc::sim::Navigator::Status::Idle);
        CHECK(world.sim.pathfinder()->requests_this_tick() == 0);
    }
    nav.set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
    CHECK(world.sim.pathfinder()->requests_this_tick() == 1);
}

TEST_CASE("A short-step obstruction check preserves factory escape and catches a fast crossing",
          "[nav][dynamic-navigation]") {
    DynamicWorld world;
    auto& unit = world.mover();
    SECTION("A unit already inside a footprint can leave it") {
        world.structure(17, 65, 12, 12);
        unit.navigator().set_goal({41, 10, 65}, world.sim.pathfinder(), unit.position(), "Land");
        REQUIRE(unit.navigator().is_moving());
        for (int tick = 0; tick < 100; ++tick)
            unit.navigator().update(unit, 8, 0.1, world.sim.terrain());
        CHECK(unit.position().x > 40);
        CHECK_FALSE(unit.navigator().busy());
    }
    SECTION("Both ends can be passable while the step crosses a new building") {
        unit.navigator().set_goal({111, 10, 65}, world.sim.pathfinder(), unit.position(), "Land");
        world.structure(65, 65, 8, 24);
        const auto before = unit.position();
        REQUIRE(unit.navigator().update(unit, 80, 1.0, world.sim.terrain()));
        CHECK(unit.position().x == before.x);
        CHECK(unit.position().z == before.z);
        CHECK(unit.navigator().status() == osc::sim::Navigator::Status::WaitingForPath);
    }
}

TEST_CASE("Unrelated structures retain accepted routes and air movement ignores ground obstacles",
          "[nav][dynamic-navigation]") {
    DynamicWorld world;
    auto& unit = world.mover();
    SECTION("A changed grid does not globally invalidate active ground routes") {
        unit.navigator().set_goal({111, 10, 65}, world.sim.pathfinder(), unit.position(), "Land");
        const auto before = DynamicWorld::nav_bytes(unit.navigator());
        world.structure(65, 105, 8, 8);
        CHECK(DynamicWorld::nav_bytes(unit.navigator()) == before);
        REQUIRE(unit.navigator().update(unit, 8, 0.1, world.sim.terrain()));
        CHECK(unit.position().x > 17);
        CHECK(unit.navigator().is_moving());
    }
    SECTION("Air slides across a newly blocked ground segment") {
        unit.set_layer("Air");
        unit.set_motion_type("RULEUMT_Air");
        unit.navigator().set_goal({111, 10, 65}, world.sim.pathfinder(), unit.position(), "Air");
        world.structure(65, 65, 8, 24);
        unit.navigator().update(unit, 80, 1.0, world.sim.terrain());
        CHECK(unit.position().x > 69);
        CHECK(unit.navigator().is_moving());
    }
}

TEST_CASE("A failed request saved before obstacle removal retries identically after restore",
          "[nav][dynamic-navigation][path-grid-invalidation][save]") {
    DynamicWorld a;
    auto& mover = a.mover();
    const auto mover_id = mover.entity_id();
    const auto building_id = a.structure(96, 96, 84, 84).entity_id();
    const osc::sim::Vector3 goal{96, 10, 96};
    mover.navigator().set_goal(goal, a.sim.pathfinder(), mover.position(), "Land");
    REQUIRE_FALSE(mover.navigator().is_moving());
    std::vector<osc::u8> saved;
    lua_settop(a.lua.raw(), 0);
    const auto save_error = osc::sim::save_snapshot(a.sim, saved);
    INFO(save_error);
    REQUIRE(save_error.empty());
    DynamicWorld b;
    lua_settop(b.lua.raw(), 0);
    const auto load_error = osc::sim::load_snapshot(b.sim, saved);
    INFO(load_error);
    REQUIRE(load_error.empty());
    auto* restored = static_cast<osc::sim::Unit*>(b.sim.entity_registry().find(mover_id));
    REQUIRE(restored);
    CHECK(DynamicWorld::nav_bytes(restored->navigator()) ==
          DynamicWorld::nav_bytes(mover.navigator()));
    a.sim.entity_registry().unregister_entity(building_id);
    b.sim.entity_registry().unregister_entity(building_id);
    for (auto* unit : {&mover, restored}) {
        auto& sim = unit == &mover ? a.sim : b.sim;
        sim.pathfinder()->reset_request_count();
        unit->navigator().set_goal(goal, sim.pathfinder(), unit->position(), "Land");
        REQUIRE(unit->navigator().is_moving());
    }
    CHECK(DynamicWorld::nav_bytes(restored->navigator()) ==
          DynamicWorld::nav_bytes(mover.navigator()));
    for (auto* unit : {&mover, restored}) {
        osc::sim::UnitCommand move;
        move.type = osc::sim::CommandType::Move;
        move.target_pos = goal;
        unit->push_command(move, true);
    }
    for (int tick = 0; tick < 30; ++tick) {
        a.sim.tick();
        b.sim.tick();
        CHECK(a.sim.compute_sync_checksum() == b.sim.compute_sync_checksum());
    }
    CHECK(mover.position().x > 17);
    CHECK(restored->position().x == mover.position().x);
    CHECK(restored->position().z == mover.position().z);
}

TEST_CASE("Passing a waypoint preserves the corner corridor and straight overshoots",
          "[nav][dynamic-navigation][waypoint-passage]") {
    DynamicWorld world;
    auto& unit = world.mover();
    const osc::sim::Vector3 goal{111, 10, 65};
    SECTION("An A star generated corner far off axis must not be skipped") {
        unit.set_position({59.4f, 10, 65});
        world.structure(65, 65, 8, 24);
        const auto path = world.sim.pathfinder()->find_path(59.4f, 65, 111, 65, "Land");
        REQUIRE(path.waypoints.size() >= 4);
        REQUIRE(path.waypoints[1].x == 59);
        REQUIRE(path.waypoints[1].z == 51);
        unit.navigator().set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
        unit.navigator().update(unit, 8, 0, world.sim.terrain());
        std::vector<osc::sim::Vector3> ahead;
        unit.navigator().path_ahead(unit.position(), 8, 8, 80, 2, ahead);
        REQUIRE(ahead.size() == 2);
        CHECK(ahead[1].x < unit.position().x);
        CHECK(ahead[1].z < unit.position().z);
    }
    SECTION("An overshoot onto the outgoing straight still advances") {
        unit.set_position({59.4f, 10, 65});
        world.structure(65, 65, 8, 24);
        unit.navigator().set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
        unit.set_position({60.5f, 10, 51});
        unit.navigator().update(unit, 8, 0, world.sim.terrain());
        std::vector<osc::sim::Vector3> ahead;
        unit.navigator().path_ahead(unit.position(), 8, 8, 80, 2, ahead);
        REQUIRE(ahead.size() == 2);
        CHECK(ahead[1].x > unit.position().x);
        CHECK(ahead[1].z == unit.position().z);
    }
    SECTION("A sidestep overshot along its straight does not trap its unit") {
        unit.navigator().set_goal(goal, world.sim.pathfinder(), unit.position(), "Land");
        unit.navigator().update(unit, 8, 0, world.sim.terrain());
        unit.navigator().sidestep({41, 10, 65});
        REQUIRE(unit.navigator().sidestepping());
        unit.set_position({42, 10, 65});
        unit.navigator().update(unit, 8, 0, world.sim.terrain());
        CHECK_FALSE(unit.navigator().sidestepping());
    }
}

TEST_CASE("A move saved while a new obstacle refuses its step resumes without stranding",
          "[nav][dynamic-navigation][save]") {
    DynamicWorld a;
    auto& mover = a.mover();
    const auto mover_id = mover.entity_id();
    osc::sim::UnitCommand move;
    move.type = osc::sim::CommandType::Move;
    move.target_pos = {111, 10, 65};
    mover.push_command(move, true);
    a.sim.tick();
    a.structure(65, 65, 8, 24);
    for (int tick = 0;
         tick < 100 && mover.navigator().status() != osc::sim::Navigator::Status::WaitingForPath;
         ++tick)
        a.sim.tick();
    REQUIRE(mover.navigator().status() == osc::sim::Navigator::Status::WaitingForPath);
    std::vector<osc::u8> saved;
    lua_settop(a.lua.raw(), 0);
    const auto save_error = osc::sim::save_snapshot(a.sim, saved);
    INFO(save_error);
    REQUIRE(save_error.empty());
    DynamicWorld b;
    lua_settop(b.lua.raw(), 0);
    const auto load_error = osc::sim::load_snapshot(b.sim, saved);
    INFO(load_error);
    REQUIRE(load_error.empty());
    auto* restored = static_cast<osc::sim::Unit*>(b.sim.entity_registry().find(mover_id));
    REQUIRE(restored);
    for (int tick = 0; tick < 250; ++tick) {
        a.sim.tick();
        b.sim.tick();
        REQUIRE(a.sim.compute_sync_checksum() == b.sim.compute_sync_checksum());
    }
    CHECK(mover.position().x > 108);
    CHECK(restored->position().x == mover.position().x);
    CHECK(mover.command_queue().empty());
    CHECK(restored->command_queue().empty());
}

TEST_CASE("Warm raw route scratch is absent from snapshots and cold restore continues identically",
          "[nav][dynamic-navigation][cache][save]") {
    DynamicWorld a;
    auto& mover = a.mover();
    const auto mover_id = mover.entity_id();
    a.structure(65, 65, 8, 24);
    const osc::sim::Vector3 goal{111.25f, 10, 65.75f};
    const auto* warm = a.sim.pathfinder();
    warm->reset_request_count();
    std::vector<osc::u8> cold_saved;
    lua_settop(a.lua.raw(), 0);
    const auto cold_save_error = osc::sim::save_snapshot(a.sim, cold_saved);
    INFO(cold_save_error);
    REQUIRE(cold_save_error.empty());
    const auto checksum_before = a.sim.compute_sync_checksum();

    const auto first =
        warm->find_path(mover.position().x, mover.position().z, goal.x, goal.z, "Land");
    REQUIRE(first.found);
    REQUIRE_FALSE(first.partial);
    const auto original_effort = warm->last_nodes_explored();
    REQUIRE(original_effort > 0);
    warm->reset_request_count();
    const auto repeated =
        warm->find_path(mover.position().x, mover.position().z, goal.x, goal.z, "Land");
    REQUIRE(repeated.found);
    REQUIRE(warm->last_nodes_explored() == original_effort);
    warm->reset_request_count();
    CHECK(a.sim.compute_sync_checksum() == checksum_before);
    std::vector<osc::u8> warm_saved;
    lua_settop(a.lua.raw(), 0);
    const auto warm_save_error = osc::sim::save_snapshot(a.sim, warm_saved);
    INFO(warm_save_error);
    REQUIRE(warm_save_error.empty());
    CHECK(warm_saved == cold_saved);

    DynamicWorld b;
    lua_settop(b.lua.raw(), 0);
    const auto load_error = osc::sim::load_snapshot(b.sim, warm_saved);
    INFO(load_error);
    REQUIRE(load_error.empty());
    auto* restored = static_cast<osc::sim::Unit*>(b.sim.entity_registry().find(mover_id));
    REQUIRE(restored);
    const auto* cold = b.sim.pathfinder();
    REQUIRE(cold);
    REQUIRE(cold->last_nodes_explored() == 0);
    warm->reset_request_count();
    cold->reset_request_count();
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto warm_result =
            warm->find_path(mover.position().x, mover.position().z, goal.x, goal.z, "Land");
        const auto cold_result =
            cold->find_path(restored->position().x, restored->position().z, goal.x, goal.z, "Land");
        REQUIRE(warm_result.found == cold_result.found);
        REQUIRE(warm_result.partial == cold_result.partial);
        REQUIRE(warm_result.throttled == cold_result.throttled);
        REQUIRE(warm_result.waypoints.size() == cold_result.waypoints.size());
        for (size_t i = 0; i < warm_result.waypoints.size(); ++i) {
            CHECK(std::bit_cast<osc::u32>(warm_result.waypoints[i].x) ==
                  std::bit_cast<osc::u32>(cold_result.waypoints[i].x));
            CHECK(std::bit_cast<osc::u32>(warm_result.waypoints[i].y) ==
                  std::bit_cast<osc::u32>(cold_result.waypoints[i].y));
            CHECK(std::bit_cast<osc::u32>(warm_result.waypoints[i].z) ==
                  std::bit_cast<osc::u32>(cold_result.waypoints[i].z));
        }
        CHECK(warm->last_nodes_explored() == original_effort);
        CHECK(cold->last_nodes_explored() == original_effort);
        CHECK(warm->requests_this_tick() == repeat + 1);
        CHECK(cold->requests_this_tick() == repeat + 1);
    }
    // No obstacle or terrain changes intervene: the warm instance and the
    // restored instance must agree while continuing from the same game state.
    for (auto* unit : {&mover, restored}) {
        osc::sim::UnitCommand move;
        move.type = osc::sim::CommandType::Move;
        move.target_pos = goal;
        unit->push_command(move, true);
    }
    for (int tick = 0; tick < 60; ++tick) {
        a.sim.tick();
        b.sim.tick();
        REQUIRE(a.sim.compute_sync_checksum() == b.sim.compute_sync_checksum());
    }
    CHECK(mover.position().x > 17);
    CHECK(restored->position().x == mover.position().x);
    CHECK(restored->position().z == mover.position().z);
}

TEST_CASE("A nearby path corner is retained while the next leg crosses an obstacle",
          "[nav][dynamic-navigation][corner-clearance]") {
    DynamicWorld world;
    world.structure(44, 75, 48, 2);
    world.structure(96, 75, 48, 2);
    auto& unit = world.mover();
    // Beside the wall, near its start cell's centre but not on the smoothed
    // centre-to-centre segment. Skipping that centre cuts the wall before
    // the gap, and every replan used to repeat the same blocked first step.
    unit.set_position({66.26294f, 10, 73.99763f});
    unit.set_orientation(osc::sim::euler_to_quat(1.362f, 0, 0));
    osc::sim::UnitCommand move;
    move.type = osc::sim::CommandType::Move;
    move.target_pos = {70, 10, 100};
    unit.push_command(move, true);
    bool crossed_wall = false;
    for (int tick = 0; tick < 300; ++tick) {
        world.sim.tick();
        const auto p = unit.position();
        crossed_wall |= p.z > 74 && p.z < 76 && ((p.x > 20 && p.x < 68) || (p.x > 72 && p.x < 120));
    }
    CHECK_FALSE(crossed_wall);
    CHECK(unit.position().z > 98);
    CHECK(unit.command_queue().empty());
}

TEST_CASE("A completed sidestep replans when its old continuation is behind a wall",
          "[nav][dynamic-navigation][sidestep-clearance]") {
    DynamicWorld world;
    world.structure(44, 75, 48, 2);
    world.structure(96, 75, 48, 2);
    auto& unit = world.mover();
    unit.set_position({66, 10, 71});
    osc::sim::UnitCommand move;
    move.type = osc::sim::CommandType::Move;
    move.target_pos = {70, 10, 100};
    unit.push_command(move, true);
    world.sim.tick();
    REQUIRE(unit.navigator().is_moving());
    unit.navigator().set_goal(move.target_pos);
    const osc::sim::Vector3 aside{65, 10, 73.8f};
    unit.navigator().sidestep(aside);
    unit.set_position(aside);
    unit.set_orientation(osc::sim::euler_to_quat(-1.5707964f, 0, 0));
    unit.note_drive(0, 0, 8, osc::sim::Unit::MotionTurn::Straight);
    unit.navigator().update(unit, 8, 0.1, world.sim.terrain());
    CHECK(unit.navigator().status() == osc::sim::Navigator::Status::WaitingForPath);
    CHECK(unit.position().x == aside.x);
    for (int tick = 0; tick < 300; ++tick) world.sim.tick();
    CHECK(unit.position().z > 98);
    CHECK(unit.command_queue().empty());
}

TEST_CASE("A unit inside its starting structure can leave directly toward its rolloff point",
          "[nav][dynamic-navigation][footprint-exit]") {
    DynamicWorld world;
    world.structure(81, 39, 6, 6);
    auto& unit = world.mover();
    unit.set_position({80, 10, 40});
    unit.set_orientation(osc::sim::euler_to_quat(-1.5707964f, 0, 0));
    REQUIRE(world.sim.pathfinding_grid()->get(40, 20) == osc::map::CellPassability::Obstacle);
    osc::sim::UnitCommand move;
    move.type = osc::sim::CommandType::Move;
    move.target_pos = {77.25f, 10, 40.35f};
    unit.push_command(move, true);
    float side = 0;
    for (int tick = 0; tick < 150 && !unit.command_queue().empty(); ++tick) {
        world.sim.tick();
        side = std::max(side, std::abs(unit.position().z - move.target_pos.z));
    }
    CHECK(side < 0.5f);
    CHECK(unit.position().x < 78);
    CHECK(unit.command_queue().empty());
}
