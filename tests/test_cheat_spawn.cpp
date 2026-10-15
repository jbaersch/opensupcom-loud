#include <catch2/catch_test_macros.hpp>

#include "blueprints/blueprint_store.hpp"
#include "core/test_status.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "sim/manipulator.hpp"
#include "sim/replay.hpp"
#include "sim/shield.hpp"
#include "sim/sim_callback_queue.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lua.h>
}

#include <limits>
#include <string>

namespace {

osc::sim::SimCallbackEntry spawn_request() {
    osc::sim::SimCallbackEntry cb;
    // Literal so the regression compiles before the missing handler exists.
    cb.func_name = "__osc_CheatCreateUnit";
    cb.args = {{"Blueprint", std::string("test_spawn")},
               {"Army", 1.0},
               {"X", 12.0},
               {"Y", 3.0},
               {"Z", 24.0}};
    return cb;
}

struct SpawnWorld {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store{lua.raw()};
    osc::sim::SimState sim{lua.raw(), &store};
    osc::sim::GameSetup setup;

    SpawnWorld() {
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_sim_bindings(lua, sim);
        sim.add_army("ARMY_1", "Player");
        sim.add_army("ARMY_2", "Opponent");
        sim.set_victory_condition("sandbox");
        REQUIRE(lua.do_string(R"(
            return {BlueprintId = 'test_spawn', Categories = {'LAND'},
                    Defense = {MaxHealth = 100}, Economy = {BuildCostMass = 25}}
        )")
                    .ok());
        store.register_blueprint(lua.raw(), osc::blueprints::BlueprintType::Unit,
                                 lua_gettop(lua.raw()));
        lua_pop(lua.raw(), 1);
        store.expose_to_lua(lua.raw());
        // Invented lifecycle callbacks; the callback must use the real
        // CreateUnit binding, so initialization cannot be bypassed.
        REQUIRE(lua.do_string(R"(
            PreCreates, Creates, Completed = 0, 0, 0
            SpawnClass = setmetatable({
                OnPreCreate = function(self) PreCreates = PreCreates + 1 end,
                OnCreate = function(self) Creates = Creates + 1 end,
                OnStopBeingBuilt = function(self, builder, layer)
                    assert(builder == nil and layer == 'Land')
                    Completed = Completed + 1
                end,
            }, {__index = moho.unit_methods})
            SpawnClass.__index = SpawnClass
        )")
                    .ok());
        lua_pushstring(lua.raw(), "__osc_unit_script_classes");
        lua_newtable(lua.raw());
        lua_pushstring(lua.raw(), "test_spawn");
        lua_getglobal(lua.raw(), "SpawnClass");
        lua_rawset(lua.raw(), -3);
        lua_rawset(lua.raw(), LUA_REGISTRYINDEX);
        setup.options.configured = true;
    }

    void enabled() {
        setup.options.set_bool("CheatsEnabled", true);
        sim.set_game_setup(setup);
    }

    void check_spawned() {
        REQUIRE(sim.entity_registry().count() == 1);
        const osc::sim::Unit* unit = nullptr;
        sim.entity_registry().for_each_unit(
            [&](osc::sim::Entity& e) { unit = static_cast<const osc::sim::Unit*>(&e); });
        REQUIRE(unit);
        CHECK(unit->army() == 1);
        CHECK(unit->blueprint_id() == "test_spawn");
        CHECK(unit->position().x == 12);
        CHECK(unit->position().y == 3);
        CHECK(unit->position().z == 24);
        CHECK_FALSE(unit->is_being_built());
        CHECK(lua.do_string("assert(PreCreates == 1 and Creates == 1 and Completed == 1)").ok());
    }
};

} // namespace

TEST_CASE("A permitted cheat spawn runs at its queued tick through the source lifecycle",
          "[simcallback][cheat-spawn]") {
    SpawnWorld world;
    world.enabled();
    world.sim.set_human_input_active(true);
    world.sim.submit_callback(spawn_request());
    CHECK(world.sim.entity_registry().count() == 0);
    REQUIRE(
        world.lua.do_string("assert(PreCreates == 0 and Creates == 0 and Completed == 0)").ok());
    world.sim.tick();
    world.check_spawned();
    CHECK(world.sim.human_input_active());
}

TEST_CASE("Cheat spawn permission comes from the latest authoritative launch option",
          "[simcallback][cheat-spawn]") {
    SpawnWorld world;
    bool permitted = false;
    SECTION("absent defaults off") {}
    SECTION("boolean enabled") {
        world.setup.options.set_bool("CheatsEnabled", true);
        permitted = true;
    }
    SECTION("native string enabled") {
        world.setup.options.set_string("CheatsEnabled", "true");
        permitted = true;
    }
    SECTION("latest false revokes earlier true") {
        world.setup.options.set_bool("CheatsEnabled", true);
        world.setup.options.set_string("CheatsEnabled", "false");
    }
    SECTION("latest true enables earlier false") {
        world.setup.options.set_bool("CheatsEnabled", false);
        world.setup.options.set_string("CheatsEnabled", "true");
        permitted = true;
    }
    SECTION("numeric truthiness is not permission") {
        world.setup.options.set_number("CheatsEnabled", 1);
    }
    world.sim.set_game_setup(world.setup);
    world.sim.submit_callback(spawn_request());
    world.sim.tick();
    CHECK(world.sim.entity_registry().count() == (permitted ? 1 : 0));
}

TEST_CASE("Malformed cheat spawn requests cannot create units", "[simcallback][cheat-spawn]") {
    SpawnWorld world;
    world.enabled();
    auto request = spawn_request();
    SECTION("army outside the game") {
        request.args["Army"] = 2.0;
    }
    SECTION("fractional army") {
        request.args["Army"] = 0.5;
    }
    SECTION("NaN army") {
        request.args["Army"] = std::numeric_limits<double>::quiet_NaN();
    }
    SECTION("NaN coordinate") {
        request.args["X"] = std::numeric_limits<double>::quiet_NaN();
    }
    SECTION("infinite coordinate") {
        request.args["Y"] = std::numeric_limits<double>::infinity();
    }
    SECTION("missing coordinate") {
        request.args.erase("Z");
    }
    SECTION("coordinate beyond the sim representation") {
        request.args["X"] = 1e100;
    }
    SECTION("non-string blueprint") {
        request.args["Blueprint"] = false;
    }
    SECTION("empty blueprint") {
        request.args["Blueprint"] = std::string();
    }
    world.sim.submit_callback(std::move(request));
    world.sim.tick();
    CHECK(world.sim.entity_registry().count() == 0);
    CHECK(world.lua.do_string("assert(PreCreates == 0 and Creates == 0 and Completed == 0)").ok());
}

TEST_CASE("Recorded cheat creation replays on the same tick with the same state",
          "[simcallback][cheat-spawn][replay]") {
    osc::sim::Replay replay;
    {
        SpawnWorld world;
        world.enabled();
        world.sim.set_recording(true);
        world.sim.tick();
        world.sim.submit_callback(spawn_request());
        world.sim.tick();
        world.check_spawned();
        REQUIRE(osc::sim::Replay::deserialize(world.sim.recorded_replay().serialize(), replay));
    }
    REQUIRE(replay.commands.size() == 1);
    REQUIRE(replay.commands[0].callback);
    CHECK(replay.commands[0].exec_tick == 2);
    SpawnWorld played;
    played.sim.set_game_setup(replay.setup);
    osc::sim::ReplayPlayback playback(replay);
    playback.start(played.sim);
    played.sim.tick();
    CHECK(played.sim.entity_registry().count() == 0);
    CHECK(playback.check(played.sim));
    played.sim.tick();
    played.check_spawned();
    CHECK(playback.check(played.sim));
}
