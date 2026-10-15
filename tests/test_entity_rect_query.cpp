#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "sim/entity.hpp"
#include "sim/manipulator.hpp"
#include "sim/projectile.hpp"
#include "sim/prop.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <memory>
#include <string>

namespace {

struct RectWorld {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};

    RectWorld() {
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_sim_bindings(lua, sim);
        sim.entity_registry().init_spatial_grid(128, 128);
    }

    template <class T> osc::sim::Entity& add(const char* name, float x = 10.0f) {
        auto e = std::make_unique<T>();
        e->set_position({x, 0, 10});
        osc::sim::CollisionShape shape;
        shape.type = osc::sim::CollisionShapeType::SPHERE;
        shape.sx = 1;
        e->set_default_collision_shape(shape);
        lua_newtable(lua.raw());
        lua_pushvalue(lua.raw(), -1);
        lua_setglobal(lua.raw(), name);
        e->set_lua_table_ref(luaL_ref(lua.raw(), LUA_REGISTRYINDEX));
        const auto id = sim.entity_registry().register_entity(std::move(e));
        return *sim.entity_registry().find(id);
    }

    void check(const char* script) {
        auto result = lua.do_string(script);
        INFO((result.ok() ? std::string() : result.error().message));
        REQUIRE(result.ok());
    }
};

} // namespace

TEST_CASE("Entity rectangle queries expose every collision family in native cell order",
          "[entity-rect]") {
    RectWorld w;
    w.add<osc::sim::Entity>("generic");
    w.add<osc::sim::Projectile>("projectile");
    w.add<osc::sim::Prop>("prop");
    w.add<osc::sim::Unit>("unit");
    w.add<osc::sim::Entity>("far", 80);
    w.check(R"(
        local found = GetEntitiesInRect(8, 8, 12, 12)
        assert(table.getn(found) == 4, 'missing collision families')
        assert(found[1] == unit and found[2] == prop, 'unit/prop family order')
        assert(found[3] == projectile and found[4] == generic, 'shared entity family order')
        local rect = {x0=8, y0=8, x1=12, y1=12}
        local again = GetEntitiesInRect(rect)
        for i, e in found do assert(again[i] == e, 'Rect differs from four coordinates') end
        assert(GetEntitiesInRect(60, 60, 64, 64) == nil, 'empty query must be nil')
        local units = GetUnitsInRect(rect)
        assert(table.getn(units) == 1 and units[1] == unit, 'unit query changed')
        local reclaim = GetReclaimablesInRect(rect)
        assert(table.getn(reclaim) == 2 and reclaim[1] == unit and reclaim[2] == prop,
               'reclaim query included projectiles/entities')
    )");
}

TEST_CASE("Entity rectangle queries retain collision bounds and tick-delayed movement",
          "[entity-rect]") {
    RectWorld w;
    auto& e = w.add<osc::sim::Projectile>("projectile", 7.5f);
    w.check("assert(GetEntitiesInRect(8,8,12,12)[1] == projectile, 'box reaching cell missed')");
    e.set_position({80, 0, 10});
    w.check("assert(GetEntitiesInRect(8,8,12,12)[1] == projectile, 'moved before AdvanceCoords')");
    w.sim.entity_registry().advance_collision_cells();
    w.check("assert(GetEntitiesInRect(8,8,12,12) == nil); "
            "assert(GetEntitiesInRect(76,8,84,12)[1] == projectile)");
    w.sim.entity_registry().unregister_entity(e.entity_id());
    w.check("assert(GetEntitiesInRect(76,8,84,12)[1] == projectile, 'removed before beat end')");
    w.sim.entity_registry().collect_garbage();
    w.check("assert(GetEntitiesInRect(76,8,84,12) == nil, 'stale entity after beat end')");
}

TEST_CASE("Restored collision links retain the shared projectile and entity order",
          "[entity-rect]") {
    RectWorld w;
    auto& a = w.add<osc::sim::Entity>("first");
    auto& b = w.add<osc::sim::Projectile>("second");
    w.check("local t=GetEntitiesInRect(8,8,12,12); assert(t[1]==second and t[2]==first)");
    auto& cells = w.sim.entity_registry().collision_cells();
    const auto links = cells.link_count();
    cells.clear();
    cells.restore({&b, &a}, links);
    CHECK(cells.link_count() == links);
    w.check("local t=GetEntitiesInRect(8,8,12,12); assert(t[1]==second and t[2]==first)");
    w.add<osc::sim::Entity>("third");
    CHECK(cells.link_count() == links + 1);
    w.check("local t=GetEntitiesInRect(8,8,12,12); "
            "assert(t[1]==third and t[2]==second and t[3]==first)");
}
