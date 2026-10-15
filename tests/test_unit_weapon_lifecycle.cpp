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
#include <lua.h>
}

TEST_CASE("Unit creation initializes source state before weapon callbacks",
          "[unit-weapon-lifecycle][lua][weapon]") {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store(lua.raw());
    osc::sim::SimState sim(lua.raw(), &store);
    lua.set_blueprint_store(&store);
    osc::lua::register_moho_bindings(lua, sim);
    osc::lua::register_sim_bindings(lua, sim);
    sim.add_army("ARMY_1", "Player");
    REQUIRE(lua.do_string(R"(
        return {
            BlueprintId = 'lifecycle_unit',
            Categories = {'LAND', 'MOBILE'},
            Physics = {MotionType = 'RULEUMT_Land'},
            Defense = {MaxHealth = 100},
            Economy = {BuildTime = 10, BuildCostMass = 1, BuildCostEnergy = 1},
            Weapon = {{Label = 'Marker', MaxRadius = 20, Damage = 1, RateOfFire = 1,
                FireTargetLayerCapsTable = {Land = 'Land|Water|Seabed'}}},
        }
    )").ok());
    store.register_blueprint(lua.raw(), osc::blueprints::BlueprintType::Unit,
                             lua_gettop(lua.raw()));
    lua_pop(lua.raw(), 1);
    store.expose_to_lua(lua.raw());
    REQUIRE(lua.do_string(R"(
        Events = {}
        PreCreates = 0
        UnitCreates = 0
        WeaponCreates = 0
        Stops = 0
        WeaponClass = {
            OnCreate = function(self)
                WeaponCreates = WeaponCreates + 1
                table.insert(Events, 'weapon')
                self.bp = self:GetBlueprint()
                RecursiveWrapperStable = self.unit:GetWeapon(1) == self
                self.AimControl = CreateAimController(self, 'Default', 0, 0, 0)
                self:SetEnabled(true)
                local layers = self.bp.FireTargetLayerCapsTable
                local current = self.unit.CacheLayer
                self:SetFireTargetLayerCaps(current and layers[current] or 'None')
                self.Initialized = true
            end,
        }
        WeaponClass.__index = function(self, key)
            return WeaponClass[key] or moho.weapon_methods[key]
        end
        __unit_class = {
            OnPreCreate = function(self)
                PreCreates = PreCreates + 1
                table.insert(Events, 'pre')
            end,
            OnCreate = function(self)
                UnitCreates = UnitCreates + 1
                table.insert(Events, 'unit')
                self.CacheLayer = self:GetCurrentLayer()
                self.EarlyWeapon = self:GetWeapon(1)
                WrapperAvailable = self.EarlyWeapon.bp and self.EarlyWeapon.Initialized and
                    self.EarlyWeapon.AimControl and
                    type(self.EarlyWeapon.AimControl.SetEnabled) == 'function'
                self.EarlyWeapon:SetEnabled(false)
                self.EarlyWeapon:ChangeMaxRadius(31)
            end,
            GetWeaponClass = function(self, label) return WeaponClass end,
            OnStopBeingBuilt = function(self)
                Stops = Stops + 1
                table.insert(Events, 'stop')
                StopSawInitializedWeapon = self:GetWeapon(1).Initialized == true
            end,
        }
        __unit_class.__index = function(self, key)
            return __unit_class[key] or moho.unit_methods[key]
        end
    )").ok());

    bool complete = false;
    SECTION("a complete CreateUnitHPR finishes after weapon initialization") {
        complete = true;
        REQUIRE(lua.do_string("created = CreateUnitHPR('lifecycle_unit', 1, 20, 0, 20, 0, 0, 0)")
                    .ok());
    }
    SECTION("a newly constructed unit initializes weapons without finishing construction") {
        lua_pushstring(lua.raw(), "__osc_create_building_unit");
        lua_rawget(lua.raw(), LUA_REGISTRYINDEX);
        lua_setglobal(lua.raw(), "CreateBuilding");
        REQUIRE(lua.do_string("createdId, created = CreateBuilding('lifecycle_unit', 1, "
                              "20, 0, 20)").ok());
    }
    CHECK(lua.do_string(R"(
        assert(PreCreates == 1 and UnitCreates == 1 and WeaponCreates == 1)
        assert(WrapperAvailable, 'source weapon data and aim must exist at first access')
        assert(RecursiveWrapperStable, 'recursive access created another wrapper')
        assert(created.EarlyWeapon == created:GetWeapon(1), 'wrapper identity changed')
        assert(created:GetWeapon(1).Initialized)
    )").ok());
    if (complete) {
        CHECK(lua.do_string("assert(table.concat(Events, ',') == 'pre,unit,weapon,stop'); "
                            "assert(Stops == 1 and StopSawInitializedWeapon)").ok());
    } else {
        CHECK(lua.do_string("assert(table.concat(Events, ',') == 'pre,unit,weapon'); "
                            "assert(Stops == 0)").ok());
    }
    REQUIRE(sim.entity_registry().count() == 1);
    osc::sim::Unit* created = nullptr;
    sim.entity_registry().for_each_unit([&](osc::sim::Entity& entity) {
        created = static_cast<osc::sim::Unit*>(&entity);
    });
    REQUIRE(created);
    REQUIRE(created->weapon_count() == 1);
    CHECK(created->get_weapon(0)->fire_target_layer_caps ==
          (osc::sim::layer_to_bit("Land") | osc::sim::layer_to_bit("Water") |
           osc::sim::layer_to_bit("Seabed")));
    CHECK(created->get_weapon(0)->max_range == 31);
    CHECK_FALSE(created->get_weapon(0)->enabled);
    CHECK(created->manipulators().size() == 1);
    CHECK(created->is_being_built() == !complete);
}
