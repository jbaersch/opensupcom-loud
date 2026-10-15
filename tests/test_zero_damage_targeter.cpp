#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "sim/bone_data.hpp"
#include "sim/manipulator.hpp"
#include "sim/shield.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <memory>

TEST_CASE("A scripted zero damage targeter acquires and aims without causing damage",
          "[weapon][zero-damage-targeter]") {
    osc::lua::LuaState lua;
    osc::sim::BoneData bones;
    osc::sim::BoneInfo turret;
    turret.name = "turret";
    bones.bones.push_back(turret);
    osc::sim::BoneInfo barrel;
    barrel.name = "barrel";
    barrel.parent_index = 0;
    bones.bones.push_back(barrel);
    osc::sim::BoneInfo muzzle;
    muzzle.name = "muzzle";
    muzzle.parent_index = 1;
    muzzle.local_position = {0, 0, 1};
    muzzle.world_position = {0, 0, 1};
    bones.bones.push_back(muzzle);
    bones.name_to_index = {{"turret", 0}, {"barrel", 1}, {"muzzle", 2}};
    osc::sim::SimState sim(lua.raw(), nullptr);
    sim.add_army("ARMY_1", "Observer");
    sim.add_army("ARMY_2", "Target");
    sim.set_victory_condition("sandbox");
    sim.set_fog_of_war("none");

    auto owner = std::make_unique<osc::sim::Unit>();
    owner->set_army(0);
    owner->set_position({40, 0, 40});
    owner->set_max_health(100);
    owner->set_health(100);
    owner->set_bone_data(&bones);
    owner->init_animated_bones();
    auto weapon = std::make_unique<osc::sim::Weapon>();
    weapon->label = "Designator";
    weapon->weapon_index = 0;
    weapon->damage = 0;
    weapon->max_range = 50;
    weapon->rate_of_fire = 1;
    weapon->script_class = true;
    weapon->fire_control_label = "Turret";
    REQUIRE(lua.do_string(R"(
        TargetEvents = 0
        TrackingEvents = 0
        LostEvents = 0
        FireEvents = 0
        return {
            OnGotTarget = function(self) TargetEvents = TargetEvents + 1 end,
            OnLostTarget = function(self) LostEvents = LostEvents + 1 end,
            OnStartTracking = function(self, label)
                assert(label == 'Turret')
                TrackingEvents = TrackingEvents + 1
            end,
            CanWeaponFire = function(self) return true end,
            OnFire = function(self) FireEvents = FireEvents + 1 end,
        }
    )").ok());
    weapon->lua_table_ref = luaL_ref(lua.raw(), LUA_REGISTRYINDEX);
    auto* designator = weapon.get();
    owner->add_weapon(std::move(weapon));
    auto controller = std::make_unique<osc::sim::AimManipulator>();
    controller->set_weapon(0, "Turret");
    controller->set_yaw_bone(0);
    controller->set_pitch_bone(1);
    controller->set_muzzle_bone(2);
    controller->set_firing_arc(-180, 180, 90, -90, 90, 90);
    auto* aim = controller.get();
    owner->add_manipulator(std::move(controller));
    auto* observer = owner.get();
    sim.entity_registry().register_entity(std::move(owner));

    auto target = std::make_unique<osc::sim::Unit>();
    target->set_army(1);
    target->set_position({60, 0, 40});
    target->set_max_health(100);
    target->set_health(100);
    const auto target_id = sim.entity_registry().register_entity(std::move(target));
    for (int tick = 0; tick < 30; ++tick) sim.tick();

    CHECK(designator->target_entity_id == target_id);
    CHECK(aim->has_target());
    CHECK(aim->on_target());
    CHECK(lua.do_string("assert(TargetEvents == 1 and TrackingEvents == 1 and FireEvents > 0)")
              .ok());
    // Other weapons mounted on this turret use this same posed barrel and
    // muzzle. The designator must turn it even though it deals no damage.
    CHECK(observer->bone_world_forward(2).x > 0.95f);
    CHECK(sim.entity_registry().find(target_id)->health() == 100);
    CHECK(sim.entity_registry().count() == 2);

    sim.entity_registry().unregister_entity(target_id);
    sim.tick();
    CHECK_FALSE(designator->has_target());
    CHECK_FALSE(aim->has_target());
    CHECK(lua.do_string("assert(LostEvents == 1)").ok());
}
