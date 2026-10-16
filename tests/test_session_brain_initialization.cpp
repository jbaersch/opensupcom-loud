#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/session_manager.hpp"
#include "sim/army_brain.hpp"
#include "sim/replay.hpp"
#include "sim/sim_state.hpp"
#include "vfs/virtual_file_system.hpp"

extern "C" {
#include <lua.h>
}

TEST_CASE("Source brain creation sees the configured faction before caching it",
          "[session][brain-initialization][replay]") {
    osc::sim::GameSetup setup;
    setup.seed = 424242;
    setup.options.configured = true;
    setup.options.set_string("PrebuiltUnits", "On");
    for (int faction = 1; faction <= 4; ++faction) {
        osc::sim::ArmySlotConfig slot;
        slot.configured = true;
        slot.human = faction % 2 == 0;
        slot.faction = faction;
        slot.slot = faction * 2;
        setup.slots.push_back(slot);
    }
    SECTION("a newly configured game") {}
    SECTION("a replay restores the same complete setup") {
        osc::sim::Replay recording;
        recording.has_setup = true;
        recording.setup = setup;
        osc::sim::Replay restored;
        REQUIRE(osc::sim::Replay::deserialize(recording.serialize(), restored));
        setup = restored.setup;
    }

    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    sim.set_seed(setup.seed);
    osc::lua::register_moho_bindings(lua, sim);
    osc::lua::ScenarioMetadata meta;
    for (int faction = 1; faction <= 4; ++faction) {
        const std::string name = "ARMY_" + std::to_string(faction * 2);
        meta.armies.push_back(name);
        sim.add_army(name, name);
    }
    osc::lua::SessionManager manager;
    manager.configure(setup);
    // Synthetic source lifecycle: native getters are the real bindings. A
    // script caches its faction once, then later chooses its initial work.
    REQUIRE(lua.do_string(R"(
        ScenarioInfo = {}
        CreatedBrains = {}
        CallbackFactions = {}
        BeginFactions = {}
        PrebuiltFactions = {}
        function SetupSession() end
        function OnCreateArmyBrain(index, brain, name)
            brain.CachedFaction = moho.aibrain_methods.GetFactionIndex(brain)
            brain.TestIndex = index
            CallbackFactions[index] = brain.CachedFaction
            CreatedBrains[name] = brain
        end
        function BeginSession()
            for name, brain in pairs(CreatedBrains) do
                BeginFactions[brain.TestIndex] = moho.aibrain_methods.GetFactionIndex(brain)
            end
        end
        function InitializePrebuiltUnits(name)
            local brain = CreatedBrains[name]
            PrebuiltFactions[brain.TestIndex] = brain.CachedFaction
        end
    )")
                .ok());
    REQUIRE(manager.start_session(lua, osc::vfs::VirtualFileSystem{}, sim, meta).ok());
    REQUIRE(manager.session_active());
    for (int faction = 1; faction <= 4; ++faction) {
        CAPTURE(faction);
        CHECK(sim.get_army(faction - 1)->faction() == faction);
        for (const char* table : {"CallbackFactions", "BeginFactions", "PrebuiltFactions"}) {
            CAPTURE(table);
            lua_getglobal(lua.raw(), table);
            lua_rawgeti(lua.raw(), -1, faction);
            CHECK(lua_tonumber(lua.raw(), -1) == faction);
            lua_pop(lua.raw(), 2);
        }
    }
}
