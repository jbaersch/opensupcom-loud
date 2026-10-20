#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "sim/army_brain.hpp"
#include "sim/manipulator.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"

#include <memory>
#include <string_view>
#include <utility>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace {

struct TransferSim {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};

    void run(std::string_view code) {
        const auto result = lua.do_string(code);
        INFO((result.ok() ? "" : result.error().message));
        REQUIRE(result.ok());
    }

    int expose(const char* name, void* object) {
        lua_State* L = lua.raw();
        lua_newtable(L);
        lua_pushstring(L, "_c_object");
        lua_pushlightuserdata(L, object);
        lua_rawset(L, -3);
        lua_pushvalue(L, -1);
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_setglobal(L, name);
        return ref;
    }

    TransferSim() {
        osc::lua::register_sim_bindings(lua, sim);
        osc::lua::register_moho_bindings(lua, sim);
        auto& army = sim.add_army("ARMY_1", "transfer test");
        army.set_lua_table_ref(expose("brain", &army));
        for (const char* name : {"member", "replacement"}) {
            auto unit = std::make_unique<osc::sim::Unit>();
            unit->set_army(0);
            unit->set_position({12, 0, 24});
            auto* raw = unit.get();
            sim.entity_registry().register_entity(std::move(unit));
            raw->set_lua_table_ref(expose(name, raw));
        }
        run(R"(
            setmetatable(brain, {__index = moho.aibrain_methods})
            source = brain:MakePlatoon('', '')
            brain:AssignUnitsToPlatoon(source, {member}, 'Attack', 'None')
            destroyed = 0
            source.OnDestroy = function(self) destroyed = destroyed + 1 end
        )");
    }
};

} // namespace

// The Lua GetPlatoonsList binder filters empty groups before native cleanup.
// Direct handles still exist and return nil for their position in this interval.
// See docs/validation/2026-10-20-platoon-transfer.md for behavioral sources.
TEST_CASE("Platoon enumeration omits a group emptied during the current tick",
          "[sim][platoon][lua][transfer]") {
    TransferSim world;
    bool survives = false;
    bool has_position = false;
    SECTION("the last member leaves") {}
    SECTION("another member stays") {
        world.run("brain:AssignUnitsToPlatoon(source, {replacement}, 'Attack', 'None')");
        survives = true;
        has_position = true;
    }
    SECTION("a replacement arrives before cleanup") {
        world.lua.set_global_bool("refill", true);
        survives = true;
        has_position = true;
    }
    SECTION("an explicitly named empty platoon survives cleanup") {
        world.run("source:UniquelyNamePlatoon('retained')");
        survives = true;
    }
    world.lua.set_global_bool("expected_survival", survives);
    world.lua.set_global_bool("expected_position", has_position);
    world.run(R"(
        assert(brain:PlatoonExists(source))
        assert(source:GetPlatoonPosition())
        ForkThread(function()
            destination = brain:MakePlatoon('', '')
            brain:AssignUnitsToPlatoon(destination, {member}, 'Attack', 'None')
            if refill then
                brain:AssignUnitsToPlatoon(source, {replacement}, 'Attack', 'None')
            end
            transferred = true
        end)
        ForkThread(function()
            assert(transferred)
            assert(brain:PlatoonExists(source))
            assert(destroyed == 0)
            local listed = false
            for _, group in ipairs(brain:GetPlatoonsList()) do
                if group == source then listed = true end
            end
            assert(listed == expected_position, 'GetPlatoonsList membership after transfer')
            local units = source:GetPlatoonUnits()
            local pos = source:GetPlatoonPosition()
            if expected_position then
                assert(table.getn(units) == 1 and units[1] == replacement)
                assert(pos and pos[1] == 12 and pos[3] == 24)
                assert(VDist3Sq({12, 0, 24}, pos) == 0)
            else
                assert(table.getn(units) == 0 and pos == nil)
                -- Catch this deliberate invalid call only in the synthetic
                -- reproducer; normal game runs must still report the fault.
                local ok, message = pcall(VDist3Sq, {12, 0, 24}, pos)
                assert(not ok and string.find(message, 'table expected'))
            end
            assert(member.PlatoonHandle == destination)
            assert(table.getn(destination:GetPlatoonUnits()) == 1)
            observed = true
        end)
    )");
    world.sim.tick();
    world.run("assert(transferred and observed)");
    world.run(R"(
        ForkThread(function()
            assert(brain:PlatoonExists(source) == expected_survival)
            assert(destroyed == (expected_survival and 0 or 1))
            assert((source:GetPlatoonPosition() ~= nil) == expected_position)
            assert(brain:PlatoonExists(destination))
            after_cleanup = true
        end)
    )");
    world.sim.tick();
    world.run("assert(after_cleanup)");
    world.sim.tick();
    world.run("assert(destroyed == (expected_survival and 0 or 1))");
}

TEST_CASE("Platoon enumeration counts retained members and excludes the army pool",
          "[sim][platoon][lua][transfer]") {
    TransferSim world;
    world.run(R"(
        pool = brain:MakePlatoon('', '')
        pool:UniquelyNamePlatoon('aRmYpOoL')
        brain:AssignUnitsToPlatoon(pool, {replacement}, 'Unassigned', 'None')
        function check_list(expect_source)
            local list = brain:GetPlatoonsList()
            assert(table.getn(list) == (expect_source and 1 or 0))
            if expect_source then assert(list[1] == source) end
            assert(brain:PlatoonExists(pool))
            assert(pool:GetPlatoonPosition())
            assert(table.getn(pool:GetPlatoonUnits()) == 1)
        end
        check_list(true)
    )");
    auto& registry = world.sim.entity_registry();
    const auto id = world.sim.army_at(0)->platoon_at(0)->unit_ids().front();
    auto* member = registry.find(id);
    REQUIRE(member != nullptr);
    member->mark_destroyed();
    registry.unregister_entity(id);
    world.run("check_list(true)");
    registry.collect_garbage([&](osc::sim::Entity& entity) {
        luaL_unref(world.lua.raw(), LUA_REGISTRYINDEX, entity.lua_table_ref());
    });
    world.run(R"(
        assert(brain:PlatoonExists(source))
        assert(source:GetPlatoonPosition() == nil)
        check_list(false)
    )");
}
