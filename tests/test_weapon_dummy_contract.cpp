#include <catch2/catch_test_macros.hpp>

#include "blueprints/blueprint_store.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "sim/manipulator.hpp"
#include "sim/shield.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <string>

namespace {

struct WeaponWorld {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store{lua.raw()};
    osc::sim::SimState sim{lua.raw(), &store};

    WeaponWorld() {
        lua.set_blueprint_store(&store);
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_sim_bindings(lua, sim);
        sim.add_army("ARMY_1", "Weapons");
        sim.set_victory_condition("sandbox");
        REQUIRE(lua.do_string(R"(
            WeaponCreates = 0
            CreatedLabels = {}
            TestWeapon = {
                OnCreate = function(self)
                    WeaponCreates = WeaponCreates + 1
                    self.bp = self:GetBlueprint()
                    table.insert(CreatedLabels, self.bp.Label)
                end,
            }
            TestWeapon.__index = function(self, key)
                return TestWeapon[key] or moho.weapon_methods[key]
            end
            __unit_class = {
                GetWeaponClass = function(self, label) return TestWeapon end,
            }
            __unit_class.__index = function(self, key)
                return __unit_class[key] or moho.unit_methods[key]
            end
        )")
                    .ok());
    }

    osc::sim::Unit* create(const char* entries) {
        REQUIRE(lua.do_string(std::string(R"(
            return {BlueprintId = 'weapon_contract',
                Categories = {'MOBILE', 'LAND'},
                Physics = {MotionType = 'RULEUMT_Land'},
                Defense = {MaxHealth = 100},
                Weapon = {
        )") + entries + "}}")
                    .ok());
        store.register_blueprint(lua.raw(), osc::blueprints::BlueprintType::Unit,
                                 lua_gettop(lua.raw()));
        lua_pop(lua.raw(), 1);
        store.expose_to_lua(lua.raw());
        REQUIRE(lua.do_string("WeaponUnit = CreateUnit('weapon_contract', 1, 20, 0, 20)").ok());
        osc::sim::Unit* result = nullptr;
        sim.entity_registry().for_each_unit(
            [&](osc::sim::Entity& e) { result = static_cast<osc::sim::Unit*>(&e); });
        REQUIRE(result);
        return result;
    }

    void check(const char* source) {
        const auto result = lua.do_string(source);
        UNSCOPED_INFO((result.ok() ? std::string() : result.error().message));
        CHECK(result.ok());
    }
};

} // namespace

TEST_CASE("Dummy blueprint weapons create no runtime object and leave dense live weapon indices",
          "[weapon][dummy-weapon]") {
    WeaponWorld world;
    const auto* unit = world.create(R"(
        {Label = 'First', MaxRadius = 20, Damage = 10},
        {Label = 'Excluded', DummyWeapon = true, MaxRadius = 999, Damage = 999},
        {Label = 'Last', DummyWeapon = false, MaxRadius = 30, Damage = 15},
    )");
    REQUIRE(unit->weapon_count() == 2);
    CHECK(unit->weapons()[0]->label == "First");
    CHECK(unit->weapons()[0]->weapon_index == 0);
    CHECK(unit->weapons()[1]->label == "Last");
    CHECK(unit->weapons()[1]->weapon_index == 1);
    world.check(R"(
        assert(WeaponUnit:GetWeaponCount() == 2)
        assert(WeaponCreates == 2, 'a dummy received source OnCreate')
        assert(table.concat(CreatedLabels, ',') == 'First,Last')
        local last = WeaponUnit:GetWeapon(2)
        assert(last == WeaponUnit:GetWeapon(2), 'weapon wrapper identity changed')
        assert(last.bp == __blueprints.weapon_contract.Weapon[3], 'original blueprint index lost')
        assert(last:GetBlueprint().MaxRadius == 30)
        assert(type(last.ChangeMaxRadius) == 'function')
        assert(type(last.SetOnTransport) == 'function')
    )");
}

TEST_CASE("Death category alone does not suppress a non-dummy runtime weapon",
          "[weapon][dummy-weapon]") {
    WeaponWorld world;
    const auto* unit = world.create(R"(
        {Label = 'DeathEvent', WeaponCategory = 'Death', DummyWeapon = false,
            FireOnDeath = true, MaxRadius = 5, Damage = 50},
    )");
    REQUIRE(unit->weapon_count() == 1);
    CHECK(unit->weapons()[0]->label == "DeathEvent");
    CHECK(unit->weapons()[0]->fire_on_death);
    world.check("assert(WeaponCreates == 1 and WeaponUnit:GetWeaponCount() == 1)");
}

TEST_CASE("A unit with only dummy weapon definitions exposes no weapons or source weapon lifecycle",
          "[weapon][dummy-weapon]") {
    WeaponWorld world;
    const auto* unit = world.create(R"(
        {Label = 'ExcludedOne', DummyWeapon = true},
        {Label = 'ExcludedTwo', DummyWeapon = true, WeaponCategory = 'Death'},
    )");
    CHECK(unit->weapon_count() == 0);
    world.check("assert(WeaponUnit:GetWeaponCount() == 0 and WeaponCreates == 0)");
}

TEST_CASE("Numeric dummy flags distinguish zero from a set flag", "[weapon][dummy-weapon]") {
    WeaponWorld world;
    const auto* unit = world.create(R"(
        {Label = 'NumericLive', DummyWeapon = 0, MaxRadius = 20, Damage = 10},
        {Label = 'NumericDummy', DummyWeapon = 1, MaxRadius = 20, Damage = 10},
    )");
    REQUIRE(unit->weapon_count() == 1);
    CHECK(unit->weapons()[0]->label == "NumericLive");
    CHECK_FALSE(unit->weapons()[0]->dummy);
    world.check("assert(WeaponUnit:GetWeaponCount() == 1 and WeaponCreates == 1)");
}
