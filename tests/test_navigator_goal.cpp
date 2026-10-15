#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "map/heightmap.hpp"
#include "map/terrain.hpp"
#include "sim/manipulator.hpp"
#include "sim/navigator.hpp"
#include "sim/shield.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <memory>
#include <vector>

TEST_CASE("Navigator GetGoalPos exposes the active orders destination",
          "[navigator-goal][nav][lua]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim(lua.raw(), nullptr);
    osc::lua::register_moho_bindings(lua, sim);
    osc::lua::register_sim_bindings(lua, sim);
    sim.add_army("ARMY_1", "Player");
    sim.add_army("ARMY_2", "Enemy");
    sim.set_victory_condition("sandbox");
    sim.set_fog_of_war("none");
    std::vector<osc::u16> heights(129 * 129, 0);
    osc::map::Heightmap heightmap(128, 128, 1.0f / 128.0f, std::move(heights));
    sim.set_terrain(std::make_unique<osc::map::Terrain>(std::move(heightmap), 0, false));
    sim.build_pathfinding_grid();

    auto unit = std::make_unique<osc::sim::Unit>();
    unit->set_army(0);
    unit->set_max_health(100);
    unit->set_health(100);
    unit->set_position({16, 0, 32});
    unit->set_max_speed(5);
    unit->set_motion_type("RULEUMT_Land");
    unit->add_command_cap("RULEUCC_Move");
    unit->add_command_cap("RULEUCC_Attack");
    auto weapon = std::make_unique<osc::sim::Weapon>();
    weapon->max_range = 5;
    unit->add_weapon(std::move(weapon));
    const auto id = sim.entity_registry().register_entity(std::move(unit));
    auto* mover = static_cast<osc::sim::Unit*>(sim.entity_registry().find(id));

    auto target = std::make_unique<osc::sim::Unit>();
    target->set_army(1);
    target->set_max_health(100);
    target->set_health(100);
    target->set_position({96, 0, 80});
    const auto target_id = sim.entity_registry().register_entity(std::move(target));

    const auto global = [&](const char* name, osc::u32 entity_id) {
        lua_State* L = lua.raw();
        lua_newtable(L);
        lua_pushstring(L, "_c_object");
        lua_pushlightuserdata(L, sim.entity_registry().find(entity_id));
        lua_rawset(L, -3);
        lua_setglobal(L, name);
    };
    global("mover", id);
    global("target", target_id);
    REQUIRE(lua.do_string(R"(
        mover.GetNavigator = moho.unit_methods.GetNavigator
        nav = mover:GetNavigator()
        assert(type(nav.GetGoalPos) == 'function', 'navigator GetGoalPos is missing')
        assert(nav:GetGoalPos() == nil, 'idle navigator has no current goal')
        IssueMove({mover}, {90, 0, 32})
    )")
                .ok());
    sim.tick();
    REQUIRE(mover->navigator().busy());
    CHECK(lua.do_string(R"(
        local goal = nav:GetGoalPos()
        assert(goal and goal[1] == 90 and goal[2] == 0 and goal[3] == 32,
               'move destination differs from its navigation goal')
        IssueClearCommands({mover})
        IssueAttack({mover}, target)
    )")
              .ok());
    sim.tick();
    CHECK(lua.do_string(R"(
        local goal = nav:GetGoalPos()
        assert(goal and goal[1] == 96 and goal[3] == 80,
               'attack navigator does not head for its target')
    )")
              .ok());
    sim.entity_registry().find(target_id)->set_position({100, 0, 90});
    sim.tick();
    CHECK(lua.do_string(R"(
        local goal = nav:GetGoalPos()
        assert(goal and goal[1] == 100 and goal[3] == 90,
               'navigation goal did not follow the moving attack target')
        IssueClearCommands({mover})
        assert(nav:GetGoalPos() == nil, 'cleared order retained its navigation goal')
        nav:SetGoal({70, 3, 45})
        local direct = nav:GetGoalPos()
        assert(direct and direct[1] == 70 and direct[2] == 3 and direct[3] == 45)
        nav:AbortMove()
        assert(nav:GetGoalPos() == nil, 'aborted navigation retained its goal')
    )")
              .ok());
    sim.entity_registry().unregister_entity(id);
    CHECK(lua.do_string("assert(nav:GetGoalPos() == nil, 'stale navigator retained a goal')").ok());
}
