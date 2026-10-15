#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/session_manager.hpp"
#include "sim/army_brain.hpp"
#include "sim/sim_state.hpp"
#include "vfs/virtual_file_system.hpp"

extern "C" {
#include <lua.h>
}

TEST_CASE("Session starts use the scenario loaded by the scripts", "[session][scenario]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    meta.armies = {"ARMY_2"};
    sim.add_army("ARMY_2", "Player");
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        local saved = {MasterChain = {_MASTERCHAIN_ = {Markers = {
            ARMY_2 = {position = {120, 7, 240}},
        }}}}
        function SetupSession() end
        function BeginSession() end
        function OnCreateArmyBrain() end
        SavedScenario = saved
    )").ok());

    SECTION("a global scenario remains supported") {
        REQUIRE(lua.do_string("Scenario = SavedScenario").ok());
    }
    SECTION("LOUD keeps its scenario in the script environment") {
        REQUIRE(lua.do_string(R"(
            ScenarioInfo.Env = {Scenario = SavedScenario}
            ScenarioLookup = false
            setmetatable(_G, {__index = function(_, key)
                if key == 'Scenario' then ScenarioLookup = true end
            end})
        )").ok());
    }
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    CHECK(sim.get_army(0)->start_position().x == 120.0f);
    CHECK(sim.get_army(0)->start_position().y == 7.0f);
    CHECK(sim.get_army(0)->start_position().z == 240.0f);
    CHECK(lua.do_string("assert(not ScenarioLookup, 'optional global lookup ran __index')").ok());
}

TEST_CASE("ArmyPool is initialized without an AI plan", "[session][armypool]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    meta.armies = {"ARMY_1"};
    sim.add_army("ARMY_1", "Player");
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        function SetupSession() end
        function BeginSession() end
        function OnCreateArmyBrain() end
        PoolCreated = false
        PoolClass = {__index = {OnCreate = function(self, plan)
            PoolCreated = true
            PoolHasPlan = plan ~= nil and plan ~= 'none'
        end}}
    )").ok());
    lua_pushstring(lua.raw(), "__platoon_class");
    lua_getglobal(lua.raw(), "PoolClass");
    lua_rawset(lua.raw(), LUA_REGISTRYINDEX);
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    CHECK(lua.do_string("assert(PoolCreated); assert(not PoolHasPlan)").ok());
}
