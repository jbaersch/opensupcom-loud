#include <catch2/catch_test_macros.hpp>

#include "blueprints/blueprint_store.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "sim/sim_state.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <string>

namespace {

struct ProjectileBlueprintWorld {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store{lua.raw()};
    osc::sim::SimState sim{lua.raw(), &store};
    osc::sim::Weapon weapon;

    ProjectileBlueprintWorld() {
        lua.set_blueprint_store(&store);
        osc::lua::register_moho_bindings(lua, sim);
        for (const char* source : {
                 "return {BlueprintId='/projectiles/first/first_proj.bp', "
                 "Economy={BuildTime=100, BuildCostMass=50, BuildCostEnergy=500}}",
                 "return {BlueprintId='/projectiles/second/second_proj.bp', "
                 "Economy={BuildTime=200, BuildCostMass=75, BuildCostEnergy=900}}",
             }) {
            REQUIRE(lua.do_string(source).ok());
            store.register_blueprint(lua.raw(), osc::blueprints::BlueprintType::Projectile,
                                     lua_gettop(lua.raw()));
            lua_pop(lua.raw(), 1);
        }
        store.expose_to_lua(lua.raw());
        weapon.projectile_bp_id = "/projectiles/first/first_proj.bp";
        lua_newtable(lua.raw());
        lua_pushstring(lua.raw(), "_c_object");
        lua_pushlightuserdata(lua.raw(), &weapon);
        lua_rawset(lua.raw(), -3);
        lua_setglobal(lua.raw(), "ProjectileWeapon");
        REQUIRE(lua.do_string(R"(
            setmetatable(ProjectileWeapon, {__index = moho.weapon_methods})
        )")
                    .ok());
    }

    void check(const char* source) {
        const auto result = lua.do_string(source);
        UNSCOPED_INFO((result.ok() ? std::string() : result.error().message));
        CHECK(result.ok());
    }
};

} // namespace

TEST_CASE("A weapon exposes its current projectile blueprint table and construction economy",
          "[weapon][projectile-blueprint]") {
    ProjectileBlueprintWorld w;
    w.check(R"(
        local bp = ProjectileWeapon:GetProjectileBlueprint()
        assert(type(bp) == 'table', 'GetProjectileBlueprint returned an id instead of a blueprint')
        assert(bp == __blueprints['/projectiles/first/first_proj.bp'], 'blueprint identity changed')
        -- Original OnSiloBuildStart stores this object for GetBuildCosts;
        -- source construction economics must read its Economy table.
        assert(bp.Economy.BuildTime == 100)
        assert(bp.Economy.BuildCostMass == 50 and bp.Economy.BuildCostEnergy == 500)
        assert(ProjectileWeapon:GetProjectileBlueprint() == bp, 'repeated lookup copied the blueprint')
    )");
}

TEST_CASE("A weapon without a known projectile blueprint returns nil",
          "[weapon][projectile-blueprint]") {
    ProjectileBlueprintWorld w;
    w.weapon.projectile_bp_id = "/projectiles/missing/missing_proj.bp";
    w.check("assert(ProjectileWeapon:GetProjectileBlueprint() == nil, 'unknown projectile was "
            "exposed')");
    w.weapon.projectile_bp_id.clear();
    w.check(
        "assert(ProjectileWeapon:GetProjectileBlueprint() == nil, 'empty projectile was exposed')");
}

TEST_CASE("Changing a weapon's projectile updates the next blueprint query",
          "[weapon][projectile-blueprint]") {
    ProjectileBlueprintWorld w;
    w.check(R"(
        local previous = ProjectileWeapon:GetProjectileBlueprint()
        ProjectileWeapon:ChangeProjectileBlueprint('/PROJECTILES/SECOND/SECOND_PROJ.BP')
        local current = ProjectileWeapon:GetProjectileBlueprint()
        assert(current == __blueprints['/projectiles/second/second_proj.bp'],
               'query retained the previous projectile or ignored canonical id lookup')
        assert(current ~= previous, 'changed projectile retained the previous table')
        assert(current.Economy.BuildTime == 200 and current.Economy.BuildCostEnergy == 900)
        assert(__blueprints['/projectiles/first/first_proj.bp'].Economy.BuildTime == 100,
               'changing the weapon mutated the old blueprint')
    )");
}
