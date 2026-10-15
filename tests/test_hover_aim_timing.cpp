#include <catch2/catch_test_macros.hpp>

#include "blueprints/blueprint_store.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "map/heightmap.hpp"
#include "map/terrain.hpp"
#include "sim/bone_data.hpp"
#include "sim/manipulator.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

TEST_CASE("A circling turret tracks this tick's aircraft attitude before firing",
          "[weapon][air-circling-aim]") {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store{lua.raw()};
    osc::sim::SimState sim{lua.raw(), &store};
    osc::lua::register_moho_bindings(lua, sim);
    osc::lua::register_sim_bindings(lua, sim);
    sim.add_army("ARMY_1", "Aircraft");
    sim.add_army("ARMY_2", "Target");
    sim.set_victory_condition("sandbox");
    sim.set_fog_of_war("none");
    osc::sim::GameSetup setup;
    setup.scenario = "/maps/synthetic/synthetic_scenario.lua";
    setup.seed = 17;
    sim.set_game_setup(setup);
    std::vector<osc::u16> heights(129 * 129, 1280);
    osc::map::Heightmap heightmap(128, 128, 1.0f / 128.0f, std::move(heights));
    sim.set_terrain(std::make_unique<osc::map::Terrain>(std::move(heightmap), 0, false));
    sim.build_pathfinding_grid();

    bool banking = false;
    bool fast_circle = false;
    bool changing_altitude = false;
    SECTION("Yawing level aircraft") {}
    SECTION("Aircraft also banks and pitches") { banking = true; }
    SECTION("A faster circle remains within the turret slew rate") {
        banking = true;
        fast_circle = true;
    }
    SECTION("A faster circle also changes its requested altitude") {
        banking = true;
        fast_circle = true;
        changing_altitude = true;
    }
    REQUIRE(lua.do_string(R"(
        return {BlueprintId = 'synthetic_hover_turret',
            Categories = {'AIR', 'MOBILE'}, Defense = {MaxHealth = 1000},
            SizeX = 2, SizeY = 1, SizeZ = 2, Footprint = {SizeX = 2, SizeZ = 2},
            Physics = {MotionType = 'RULEUMT_Air', Elevation = 10},
            Air = {CanFly = true, Winged = false, MaxAirspeed = 10, MinAirspeed = 3,
                TurnSpeed = 1, StartTurnDistance = 2, KMove = 0.9, KMoveDamping = 1.5,
                KTurn = 1.2, KTurnDamping = 2, KRoll = 1.5, KRollDamping = 2,
                KLift = 2, KLiftDamping = 2, LiftFactor = 5, CirclingDirChange = false,
                CirclingRadiusChangeMinRatio = 0.7, CirclingRadiusChangeMaxRatio = 0.7,
                CirclingElevationChangeRatio = 0}}
    )").ok());
    lua_State* L = lua.raw();
    lua_pushstring(L, "Air");
    lua_rawget(L, -2);
    lua_pushstring(L, "BankFactor");
    lua_pushnumber(L, banking ? 0.15 : 0);
    lua_rawset(L, -3);
    lua_pushstring(L, "BankForward");
    lua_pushboolean(L, banking);
    lua_rawset(L, -3);
    const auto air_number = [&](const char* key, float value) {
        lua_pushstring(L, key);
        lua_pushnumber(L, value);
        lua_rawset(L, -3);
    };
    if (fast_circle) {
        // At radius 24, 14 units/s corresponds to about 33 degrees/s,
        // below the turret's 40 degrees/s but above its half-degree
        // tolerance during a single simulation tick.
        air_number("MaxAirspeed", 14);
        air_number("MinAirspeed", 14);
        air_number("CirclingRadiusChangeMinRatio", 0.8f);
        air_number("CirclingRadiusChangeMaxRatio", 0.8f);
    }
    if (changing_altitude) air_number("CirclingElevationChangeRatio", 0.5f);
    lua_pop(L, 1);
    store.register_blueprint(L, osc::blueprints::BlueprintType::Unit, lua_gettop(L));
    lua_pop(L, 1);
    store.expose_to_lua(L);
    REQUIRE(lua.do_string(R"(
        SyntheticAircraft = setmetatable({}, {__index = moho.unit_methods})
        SyntheticAircraft.__index = SyntheticAircraft
    )").ok());
    lua_pushstring(L, "__osc_unit_script_classes");
    lua_newtable(L);
    lua_pushstring(L, "synthetic_hover_turret");
    lua_getglobal(L, "SyntheticAircraft");
    lua_rawset(L, -3);
    lua_rawset(L, LUA_REGISTRYINDEX);
    REQUIRE(lua.do_string("aircraft = CreateUnit('synthetic_hover_turret', 1, 88, 20, 64)").ok());
    osc::sim::Unit* aircraft = nullptr;
    sim.entity_registry().for_each_unit([&](osc::sim::Entity& entity) {
        auto& unit = static_cast<osc::sim::Unit&>(entity);
        if (unit.blueprint_id() == "synthetic_hover_turret") aircraft = &unit;
    });
    REQUIRE(aircraft);

    osc::sim::BoneData bones;
    osc::sim::BoneInfo yaw;
    yaw.name = "yaw";
    bones.bones.push_back(yaw);
    osc::sim::BoneInfo pitch;
    pitch.name = "pitch";
    pitch.parent_index = 0;
    bones.bones.push_back(pitch);
    osc::sim::BoneInfo muzzle;
    muzzle.name = "muzzle";
    muzzle.parent_index = 1;
    muzzle.local_position = {0, 0, 0.5f};
    muzzle.world_position = muzzle.local_position;
    bones.bones.push_back(muzzle);
    bones.name_to_index = {{"yaw", 0}, {"pitch", 1}, {"muzzle", 2}};
    aircraft->set_bone_data(&bones);
    aircraft->init_animated_bones();

    auto gun = std::make_unique<osc::sim::Weapon>();
    gun->label = "SyntheticTurret";
    gun->weapon_index = 0;
    gun->max_range = 30;
    gun->damage = 10;
    gun->rate_of_fire = 1;
    gun->firing_tolerance = 0.5f;
    gun->script_class = true;
    gun->fire_control_label = "Aim";
    gun->muzzle_bone_name = "muzzle";
    REQUIRE(lua.do_string(R"(
        return {CanWeaponFire = function(self) return true end,
                OnFire = function(self) self.Fired = (self.Fired or 0) + 1 end}
    )").ok());
    gun->lua_table_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    auto* weapon = gun.get();
    aircraft->add_weapon(std::move(gun));
    auto controller = std::make_unique<osc::sim::AimManipulator>();
    controller->set_weapon(0, "Aim");
    controller->set_yaw_bone(0);
    controller->set_pitch_bone(1);
    controller->set_muzzle_bone(2);
    controller->set_firing_arc(-180, 180, 40, -85, 85, 240);
    auto* aim = controller.get();
    aircraft->add_manipulator(std::move(controller));

    auto target_unit = std::make_unique<osc::sim::Unit>();
    target_unit->set_army(1);
    target_unit->add_category("LAND");
    target_unit->set_position({64, 10, 64});
    target_unit->set_max_health(1000);
    target_unit->set_health(1000);
    const auto target_id = sim.entity_registry().register_entity(std::move(target_unit));
    osc::sim::UnitCommand attack;
    attack.type = osc::sim::CommandType::Attack;
    attack.target_id = target_id;
    attack.target_pos = {64, 10, 64};
    aircraft->push_command(attack, true);
    for (int tick = 0; tick < 300; ++tick) sim.tick();
    REQUIRE(aircraft->air_combat().flying);
    REQUIRE(weapon->target_entity_id == target_id);
    const auto initial_shots = weapon->shots_at_target;
    int in_range_ticks = 0;
    int aligned_ticks = 0;
    float path = 0;
    float largest_body_step = 0;
    auto last = aircraft->position();
    for (int tick = 0; tick < 400; ++tick) {
        const auto shots = weapon->shots_at_target;
        const auto before = aircraft->orientation();
        sim.tick();
        const auto after = aircraft->orientation();
        const float body_dot = std::abs(before.x * after.x + before.y * after.y +
                                        before.z * after.z + before.w * after.w);
        largest_body_step = std::max(largest_body_step,
                                    2.0f * std::acos(std::min(body_dot, 1.0f)));
        const auto position = aircraft->position();
        path += std::hypot(position.x - last.x, position.z - last.z);
        last = position;
        if (weapon->in_firing_range(*aircraft, attack.target_pos)) ++in_range_ticks;
        if (aim->on_target()) ++aligned_ticks;
        if (weapon->shots_at_target <= shots) continue;
        const auto from = aircraft->bone_world_position(2);
        const auto forward = aircraft->bone_world_forward(2);
        // This single weapon's update sets the aim point immediately
        // before OnFire. The target is stationary and no other controller
        // changes it later in this tick. Its collision target point may
        // differ from the entity position used by the attack order.
        const auto fired_at = aim->target();
        const auto dx = fired_at.x - from.x;
        const auto dy = fired_at.y - from.y;
        const auto dz = fired_at.z - from.z;
        const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        REQUIRE(distance > 0);
        // Both half-degree lanes together remain within one degree of the
        // actual muzzle-to-target line. Never grant fire by widening them.
        CHECK((forward.x * dx + forward.y * dy + forward.z * dz) / distance > 0.9998f);
    }
    const auto shots = weapon->shots_at_target - initial_shots;
    INFO("banking " << banking << " fast " << fast_circle << " changing altitude "
                    << changing_altitude << " path " << path << " body step " << largest_body_step
                    << " in range " << in_range_ticks << " aligned " << aligned_ticks
                    << " shots " << shots);
    CHECK(path > 20);
    CHECK(in_range_ticks > 380);
    // Forty seconds in a settled, reachable circle must not starve a 1 Hz
    // turret merely because its parent changes orientation each tick.
    CHECK(aligned_ticks > 300);
    CHECK(shots >= 30);
}
