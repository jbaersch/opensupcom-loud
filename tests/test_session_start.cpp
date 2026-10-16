#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/session_manager.hpp"
#include "sim/army_brain.hpp"
#include "sim/build_info.hpp"
#include "sim/saved_game.hpp"
#include "sim/sim_state.hpp"
#include "vfs/virtual_file_system.hpp"

extern "C" {
#include <lua.h>
}

TEST_CASE("Launch player data reaches the scripts through reload and saved history",
          "[session][player-options][replay][save]") {
    // Invented lobby data: RGB colours and LOUD's per-player multiplier and
    // adaptive mode must survive alongside fields the engine does not know.
    osc::lua::LuaState launch;
    REQUIRE(launch
                .do_string(R"(
        config = {PlayerOptions = {[3] = {
            Human = false, Civilian = false, Faction = 2, Team = 4,
            StartSpot = 1, PlayerName = 'Test opponent', AIPersonality = 'test_ai',
            WheelColor = {17, 83, 211}, PlayerColor = {19, 37, 59},
            Mult = '1.35', ACT = 4, Ready = false,
            Extension = {enabled = true, values = {[2] = 'keep me'}},
            ArmyName = 'cannot replace scenario identity', ArmyIndex = 99,
        }}}
    )")
                .ok());
    SECTION("single player teamInfo uses the same contract") {
        REQUIRE(launch
                    .do_string("config.teamInfo = config.PlayerOptions; "
                               "config.PlayerOptions = nil")
                    .ok());
    }
    lua_getglobal(launch.raw(), "config");
    auto setup = osc::lua::read_session_config(launch.raw(), lua_gettop(launch.raw()));
    lua_pop(launch.raw(), 1);
    REQUIRE(setup.slots.size() == 1);

    osc::sim::Replay recording;
    recording.has_setup = true;
    recording.setup = setup;
    SECTION("recorded launch survives a replay round trip") {
        osc::sim::Replay restored;
        REQUIRE(osc::sim::Replay::deserialize(recording.serialize(), restored));
        setup = restored.setup;
    }
    SECTION("saved history preserves the recorded launch") {
        osc::sim::SavedGame save;
        save.build = osc::sim::build_id();
        save.game = recording;
        osc::sim::SavedGame restored;
        REQUIRE(osc::sim::SavedGame::deserialize(save.serialize(), restored) ==
                osc::sim::SaveLoadError::None);
        setup = restored.game.setup;
    }

    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    meta.armies = {"ARMY_3"};
    sim.add_army("ARMY_3", "ARMY_3");
    manager.configure(setup);
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        function SetupSession() end
        function BeginSession() end
        function OnCreateArmyBrain(index, brain, name, nickname)
            ObservedNickname = nickname
        end
    )")
                .ok());
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    CHECK(sim.get_army(0)->nickname() == "Test opponent");
    CHECK(lua.do_string(R"(
        local p = ScenarioInfo.ArmySetup.ARMY_3
        assert(p.WheelColor and p.WheelColor[1] == 17 and
               p.WheelColor[2] == 83 and p.WheelColor[3] == 211, 'RGB colour lost')
        assert(type(p.PlayerColor) == 'table' and p.PlayerColor[2] == 37,
               'table colour replaced by an index')
        assert(p.Mult == '1.35' and p.ACT == 4, 'AI configuration lost')
        assert(p.PlayerName == 'Test opponent' and p.Nickname == 'Test opponent')
        assert(ObservedNickname == 'Test opponent', 'brain nickname lost')
        assert(p.Ready == false and p.Extension.enabled and
               p.Extension.values[2] == 'keep me', 'other player data lost')
        assert(p.ArmyName == 'ARMY_3' and p.ArmyIndex == 1, 'army identity changed')
        assert(p.Human == false and p.Civilian == false and
               p.AIPersonality == 'test_ai' and p.Faction == 2 and p.Team == 4)
    )")
              .ok());
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
    )")
                .ok());

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
        )")
                    .ok());
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
    )")
                .ok());
    lua_pushstring(lua.raw(), "__platoon_class");
    lua_getglobal(lua.raw(), "PoolClass");
    lua_rawset(lua.raw(), LUA_REGISTRYINDEX);
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    CHECK(lua.do_string("assert(PoolCreated); assert(not PoolHasPlan)").ok());
}

TEST_CASE("Uncarryable player slots fail before scripts start", "[session][player-options]") {
    osc::lua::LuaState lua;
    REQUIRE(lua.do_string(R"(
        local player = {Human = true}
        player.cycle = player
        config = {PlayerOptions = {[1] = player}}
        ScenarioInfo = {}
        Started = false
        function SetupSession() Started = true end
    )")
                .ok());
    lua_getglobal(lua.raw(), "config");
    const auto setup = osc::lua::read_session_config(lua.raw(), lua_gettop(lua.raw()));
    lua_pop(lua.raw(), 1);
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::SessionManager manager;
    osc::lua::ScenarioMetadata meta;
    meta.armies = {"ARMY_1"};
    sim.add_army("ARMY_1", "Player");
    manager.configure(setup);
    const int top = lua_gettop(lua.raw());
    const auto result = manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("Player slot 1") != std::string::npos);
    CHECK(lua_gettop(lua.raw()) == top);
    CHECK(lua.do_string("assert(not Started)").ok());
}
