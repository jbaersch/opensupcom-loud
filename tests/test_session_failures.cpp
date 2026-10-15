#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/session_manager.hpp"
#include "sim/sim_state.hpp"
#include "vfs/virtual_file_system.hpp"

extern "C" {
#include <lua.h>
}

#include <string>

TEST_CASE("Source session failures stop launch before later lifecycle stages",
          "[session][startup-failure]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    meta.armies = {"ARMY_1", "ARMY_2"};
    sim.add_army("ARMY_1", "Player");
    sim.add_army("ARMY_2", "Opponent");
    osc::lua::GameOptionsConfig options;
    options.configured = true;
    options.set_string("PrebuiltUnits", "On");
    manager.set_game_options(options);
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        SetupCalls = 0
        BrainCalls = 0
        BeginCalls = 0
        PrebuiltCalls = 0
        ReplacementCalls = 0
        function SetupSession() SetupCalls = SetupCalls + 1 end
        function OnCreateArmyBrain() BrainCalls = BrainCalls + 1 end
        function BeginSession() BeginCalls = BeginCalls + 1 end
        function InitializePrebuiltUnits() PrebuiltCalls = PrebuiltCalls + 1 end
        function CreateUnit()
            ReplacementCalls = ReplacementCalls + 1
            return {}
        end
    )")
                .ok());

    std::string diagnostic;
    std::string observed_calls;
    int expected_prebuilt_calls = 0;
    SECTION("SetupSession error prevents brain creation and BeginSession") {
        REQUIRE(lua.do_string(R"(
            function SetupSession()
                SetupCalls = SetupCalls + 1
                error('synthetic setup failure')
            end
        )")
                    .ok());
        diagnostic = "synthetic setup failure";
        observed_calls = "assert(SetupCalls == 1 and BrainCalls == 0 and BeginCalls == 0)";
    }
    SECTION("BeginSession error prevents prebuilt and replacement units") {
        REQUIRE(lua.do_string(R"(
            function BeginSession()
                BeginCalls = BeginCalls + 1
                error('synthetic begin failure')
            end
        )")
                    .ok());
        diagnostic = "synthetic begin failure";
        observed_calls = "assert(SetupCalls == 1 and BrainCalls == 2 and BeginCalls == 1)";
    }
    SECTION("one failed army brain prevents a partially initialized game") {
        REQUIRE(lua.do_string(R"(
            function OnCreateArmyBrain(index)
                BrainCalls = BrainCalls + 1
                if index == 1 then error('synthetic brain failure') end
            end
        )")
                    .ok());
        diagnostic = "synthetic brain failure";
        observed_calls = "assert(SetupCalls == 1 and BrainCalls == 1 and BeginCalls == 0)";
    }

    SECTION("prebuilt error prevents replacement units and activation") {
        REQUIRE(lua.do_string(R"(
            function InitializePrebuiltUnits()
                PrebuiltCalls = PrebuiltCalls + 1
                error('synthetic prebuilt failure')
            end
        )")
                    .ok());
        diagnostic = "synthetic prebuilt failure";
        observed_calls = "assert(SetupCalls == 1 and BrainCalls == 2 and BeginCalls == 1)";
        expected_prebuilt_calls = 1;
    }
    const auto result = manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta);
    CHECK_FALSE(result.ok());
    if (!result.ok()) CHECK(result.error().message.find(diagnostic) != std::string::npos);
    CHECK_FALSE(manager.session_active());
    lua_pushstring(lua.raw(), "osc_session_active");
    lua_rawget(lua.raw(), LUA_REGISTRYINDEX);
    CHECK_FALSE(lua_toboolean(lua.raw(), -1));
    lua_pop(lua.raw(), 1);
    CHECK(sim.entity_registry().count() == 0);
    CHECK(lua.do_string(observed_calls).ok());
    CHECK(lua.do_string("assert(PrebuiltCalls == " + std::to_string(expected_prebuilt_calls) +
                        " and ReplacementCalls == 0)")
              .ok());
}

TEST_CASE("A failed restart clears the previous session active state",
          "[session][startup-failure]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        function SetupSession() end
        function BeginSession() end
    )")
                .ok());
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    REQUIRE(manager.session_active());
    REQUIRE(lua.do_string(R"(
        function SetupSession() error('synthetic restart failure') end
    )")
                .ok());
    const auto result = manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(manager.session_active());
    lua_pushstring(lua.raw(), "osc_session_active");
    lua_rawget(lua.raw(), LUA_REGISTRYINDEX);
    CHECK_FALSE(lua_toboolean(lua.raw(), -1));
    lua_pop(lua.raw(), 1);
}

TEST_CASE("A source session owns delayed commander creation", "[session][startup-failure]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    meta.armies = {"ARMY_1"};
    sim.add_army("ARMY_1", "Player");
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        ReplacementCalls = 0
        function SetupSession() end
        function OnCreateArmyBrain() end
        function BeginSession() end
        function CreateUnit() ReplacementCalls = ReplacementCalls + 1 end
    )")
                .ok());
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    CHECK(lua.do_string("assert(ReplacementCalls == 0)").ok());
    CHECK(sim.entity_registry().count() == 0);
}
