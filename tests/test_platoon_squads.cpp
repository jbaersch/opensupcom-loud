#include <catch2/catch_test_macros.hpp>

#include "sim/platoon.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/manipulator.hpp"
#include "sim/shield.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

using osc::sim::Platoon;

TEST_CASE("Platoon membership survives Destroy until the deletion queue drains",
          "[sim][platoon][lua]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::register_moho_bindings(lua, sim);
    auto& registry = sim.entity_registry();
    auto unit = std::make_unique<osc::sim::Unit>();
    unit->set_position({12, 3, 24});
    auto* member = unit.get();
    const auto id = registry.register_entity(std::move(unit));
    Platoon platoon;
    platoon.add_unit(id);
    lua_State* L = lua.raw();
    lua_newtable(L);
    lua_pushstring(L, "_c_object");
    lua_pushlightuserdata(L, member);
    lua_rawset(L, -3);
    lua_pushvalue(L, -1);
    member->set_lua_table_ref(luaL_ref(L, LUA_REGISTRYINDEX));
    lua_setglobal(L, "member");
    lua_newtable(L);
    lua_pushstring(L, "_c_object");
    lua_pushlightuserdata(L, &platoon);
    lua_rawset(L, -3);
    lua_setglobal(L, "group");
    REQUIRE(lua.do_string("setmetatable(group, {__index = moho.platoon_methods})").ok());
    const auto check_member = [&] {
        CHECK(lua.do_string(R"(
            local pos = group:GetPlatoonPosition()
            assert(pos and pos[1] == 12 and pos[2] == 3 and pos[3] == 24)
            local units = group:GetPlatoonUnits()
            assert(table.getn(units) == 1 and units[1] == member)
        )")
                  .ok());
    };
    check_member();
    member->mark_destroyed();
    registry.unregister_entity(id);
    REQUIRE(registry.find(id) == nullptr); // no longer a live world entity
    check_member(); // still owned, with a script handle and platoon membership
    registry.collect_garbage(
        [&](osc::sim::Entity& e) { luaL_unref(L, LUA_REGISTRYINDEX, e.lua_table_ref()); });
    CHECK(lua.do_string(R"(
        assert(group:GetPlatoonPosition() == nil)
        assert(table.getn(group:GetPlatoonUnits()) == 0)
    )")
              .ok());
}

TEST_CASE("Platoon squads: Moho's classes read in any case", "[sim][platoon]") {
    CHECK(Platoon::squad_class("Unassigned") == 0);
    CHECK(Platoon::squad_class("") == 0);
    CHECK(Platoon::squad_class("Attack") == 1);
    CHECK(Platoon::squad_class("artillery") == 2);
    CHECK(Platoon::squad_class("GUARD") == 3);
    CHECK(Platoon::squad_class("Support") == 4);
    CHECK(Platoon::squad_class("scout") == 5);
    CHECK(Platoon::squad_class("Scouts") == -1);
    CHECK(Platoon::squad_class("TransportPool") == -1);
}

TEST_CASE("Platoon squads: a unit is in its squad by class, else by name", "[sim][platoon]") {
    Platoon p;
    p.add_unit(1);
    p.set_unit_squad(1, "Scout");
    p.add_unit(2);
    p.set_unit_squad(2, "MySquad");
    p.add_unit(3); // no squad: Unassigned

    CHECK(p.in_squad(1, "scout"));
    CHECK(p.in_squad(1, "SCOUT"));
    CHECK_FALSE(p.in_squad(1, "Attack"));
    CHECK(p.in_squad(2, "MySquad"));
    CHECK_FALSE(p.in_squad(2, "mysquad"));
    CHECK_FALSE(p.in_squad(2, "Unassigned"));
    CHECK(p.in_squad(3, "unassigned"));
    CHECK_FALSE(p.in_squad(3, "Scout"));
}
