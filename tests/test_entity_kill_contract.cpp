#include <catch2/catch_test_macros.hpp>

#include "core/test_status.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "sim/army_brain.hpp"
#include "sim/manipulator.hpp"
#include "sim/shield.hpp"
#include "sim/weapon.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <memory>
#include <string>

namespace {

struct DamageWorld {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};
    osc::sim::Unit* attacker = nullptr;
    osc::sim::Unit* victim = nullptr;

    DamageWorld() {
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_sim_bindings(lua, sim);
        sim.add_army("ARMY_1", "Attacker");
        sim.add_army("ARMY_2", "Victim");
        attacker = add_unit("attacker", 0);
        victim = add_unit("victim", 1);
        // Invented script: cache the base primitives, as callers may do,
        // and let the source callback decide when damage becomes a kill.
        REQUIRE(lua.do_string(R"(
            local kill = moho.entity_methods.Kill
            local adjust = moho.entity_methods.AdjustHealth
            local health = moho.entity_methods.GetHealth
            KillCalls = 0
            function victim:OnDamage(instigator, amount, direction, damageType)
                adjust(self, instigator, -amount)
                if health(self) <= 0 then kill(self, instigator, damageType, 0.25) end
            end
            function victim:OnKilled(instigator, damageType, excess)
                KillCalls = KillCalls + 1
                assert(instigator == attacker and damageType == 'Normal' and excess == 0.25)
                self.Dead = true
            end
            function RepeatKill()
                kill(victim, attacker, 'Normal', 0.25)
            end
        )")
                    .ok());
    }

    osc::sim::Unit* add_unit(const char* name, int army) {
        auto unit = std::make_unique<osc::sim::Unit>();
        unit->set_army(army);
        unit->set_blueprint_id(name);
        unit->set_max_health(100);
        unit->set_health(100);
        unit->set_fraction_complete(1);
        auto* result = unit.get();
        sim.entity_registry().register_entity(std::move(unit));
        lua_State* L = lua.raw();
        lua_newtable(L);
        const int table = lua_gettop(L);
        lua_pushstring(L, "_c_object");
        lua_pushlightuserdata(L, result);
        lua_rawset(L, table);
        lua_pushstring(L, "_c_sim_gen");
        lua_pushnumber(L, osc::sim::SimState::sim_generation());
        lua_rawset(L, table);
        lua_getglobal(L, "moho");
        lua_pushstring(L, "unit_methods");
        lua_rawget(L, -2);
        lua_setmetatable(L, table);
        lua_pop(L, 1);
        lua_pushvalue(L, table);
        result->set_lua_table_ref(luaL_ref(L, LUA_REGISTRYINDEX));
        lua_setglobal(L, name);
        return result;
    }

    void damage() {
        REQUIRE(lua.do_string("Damage(attacker, {0, 0, 0}, victim, 125, 'Normal')").ok());
    }
};

struct FailureCounting {
    bool previous = osc::test_status::count_lua_failures();
    FailureCounting() {
        osc::test_status::reset();
        osc::test_status::set_count_lua_failures(true);
    }
    ~FailureCounting() {
        osc::test_status::set_count_lua_failures(previous);
        osc::test_status::reset();
    }
};

} // namespace

TEST_CASE("Cached base entity Kill completes source-decided lethal damage once",
          "[entity][kill-contract]") {
    DamageWorld world;
    world.damage();
    CHECK(world.victim->health() == 0);
    CHECK(world.victim->is_dying());
    CHECK(world.victim->do_not_target());
    CHECK_FALSE(world.victim->destroyed()); // its script owns the death sequence
    CHECK(world.sim.get_army(0)->get_stat("Enemies_Killed") == 1);
    CHECK(world.sim.get_army(1)->get_stat("Units_Killed") == 1);
    REQUIRE(world.lua.do_string("RepeatKill(); assert(KillCalls == 1 and victim.Dead)").ok());
    CHECK(world.sim.get_army(0)->get_stat("Enemies_Killed") == 1);
    CHECK(world.sim.get_army(1)->get_stat("Units_Killed") == 1);
}

TEST_CASE("Base entity Kill retains native and script kill protection", "[entity][kill-contract]") {
    DamageWorld world;
    SECTION("native protection") {
        world.victim->set_can_be_killed(false);
    }
    SECTION("script protection") {
        REQUIRE(world.lua.do_string("victim.CanBeKilled = false").ok());
    }
    world.damage();
    CHECK_FALSE(world.victim->is_dying());
    CHECK_FALSE(world.victim->destroyed());
    CHECK(world.sim.get_army(0)->get_stat("Enemies_Killed") == 0);
    CHECK(world.sim.get_army(1)->get_stat("Units_Killed") == 0);
    CHECK(world.lua.do_string("assert(KillCalls == 0)").ok());
}

TEST_CASE("OnDamage callback errors count as test failures without leaking Lua stack",
          "[entity][damage-error]") {
    DamageWorld world;
    REQUIRE(world.lua
                .do_string(R"(
        function victim:OnDamage() error('synthetic damage callback failure') end
    )")
                .ok());
    FailureCounting counting;
    const int top = lua_gettop(world.lua.raw());
    world.damage();
    CHECK(lua_gettop(world.lua.raw()) == top);
    CHECK(osc::test_status::failure_count() == 1);
    const auto messages = osc::test_status::failure_messages();
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].find("OnDamage") != std::string::npos);
    CHECK(messages[0].find("synthetic damage callback failure") != std::string::npos);
}
