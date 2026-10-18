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
#include <cmath>

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
    )")
                .ok());
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
    CHECK(
        lua.do_string("assert(TargetEvents == 1 and TrackingEvents == 1 and FireEvents > 0)").ok());
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

TEST_CASE("A narrow weapon aims relative to another controller on its shared turret",
          "[weapon][zero-damage-targeter][shared-aim]") {
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
    bool offset_muzzle = false;
    SECTION("The barrel shares the yaw pivot") {}
    SECTION("The shared barrel has a tilted bind pose") {
        bones.bones[1].local_rotation = osc::sim::quat_axis_angle('x', -0.5235987756f);
    }
    SECTION("The barrel and muzzle are offset from the yaw pivot") {
        offset_muzzle = true;
        bones.bones[1].local_position = {4, 2, 0};
        bones.bones[1].world_position = {4, 2, 0};
        bones.bones[2].world_position = {4, 2, 1};
    }
    osc::sim::SimState sim(lua.raw(), nullptr);
    sim.add_army("ARMY_1", "Observer");
    sim.add_army("ARMY_2", "Targets");
    sim.set_victory_condition("sandbox");
    sim.set_fog_of_war("none");

    auto owner = std::make_unique<osc::sim::Unit>();
    owner->set_army(0);
    owner->set_position({40, 0, 40});
    owner->set_max_health(100);
    owner->set_health(100);
    owner->set_bone_data(&bones);
    owner->init_animated_bones();
    osc::sim::Weapon* weapons[2]{};
    osc::sim::AimManipulator* aims[2]{};
    for (int index = 0; index < 2; ++index) {
        auto weapon = std::make_unique<osc::sim::Weapon>();
        weapon->weapon_index = index;
        weapon->damage = index == 0 ? 0 : 10;
        weapon->max_range = 50;
        weapon->rate_of_fire = 1;
        weapon->firing_tolerance = 1;
        weapon->script_class = true;
        weapon->fire_control_label = "Turret";
        REQUIRE(lua.do_string(R"(
            return {CanWeaponFire = function(self) return true end,
                    OnFire = function(self) self.Fired = (self.Fired or 0) + 1 end}
        )")
                    .ok());
        weapon->lua_table_ref = luaL_ref(lua.raw(), LUA_REGISTRYINDEX);
        weapons[index] = weapon.get();
        owner->add_weapon(std::move(weapon));
        auto controller = std::make_unique<osc::sim::AimManipulator>();
        controller->set_weapon(index, "Turret");
        controller->set_yaw_bone(0);
        controller->set_pitch_bone(1);
        controller->set_muzzle_bone(2);
        controller->set_precedence(10);
        const float arc = index == 0 ? 180.0f : 10.0f;
        const float pitch_arc = index == 0 ? 90.0f : 10.0f;
        controller->set_firing_arc(-arc, arc, 90, -pitch_arc, pitch_arc, 90);
        aims[index] = controller.get();
        owner->add_manipulator(std::move(controller));
    }
    auto* observer = owner.get();
    sim.entity_registry().register_entity(std::move(owner));
    const auto make_target = [&](float x, float z) {
        auto unit = std::make_unique<osc::sim::Unit>();
        unit->set_army(1);
        unit->set_position({x, 0, z});
        unit->set_max_health(100);
        unit->set_health(100);
        return sim.entity_registry().register_entity(std::move(unit));
    };
    const auto east = make_target(60, 40);
    const auto ticks = [&](int count) {
        for (int tick = 0; tick < count; ++tick) {
            const auto shots = weapons[1]->shots_at_target;
            sim.tick();
            if (weapons[1]->shots_at_target <= shots) continue;
            const auto* target = sim.entity_registry().find(weapons[1]->target_entity_id);
            REQUIRE(target);
            const auto from = observer->bone_world_position(2);
            const auto forward = observer->bone_world_forward(2);
            const auto dx = target->position().x - from.x;
            const auto dy = target->position().y - from.y;
            const auto dz = target->position().z - from.z;
            const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            REQUIRE(distance > 0);
            CHECK((forward.x * dx + forward.y * dy + forward.z * dz) / distance > 0.999f);
        }
    };
    ticks(60);
    REQUIRE(weapons[0]->target_entity_id == east);
    REQUIRE(weapons[1]->target_entity_id == east);
    CHECK(aims[0]->on_target());
    CHECK(aims[1]->on_target());
    CHECK(weapons[1]->shots_at_target > 0);
    // The combined physical muzzle must point toward the eastward target.
    // An offset barrel needs parallax correction, so its direction differs
    // from the line through the yaw pivot; every shot above checks the
    // actual line from the muzzle to its own target.
    CHECK(observer->bone_world_forward(2).x > (offset_muzzle ? 0.95f : 0.999f));
    CHECK(sim.entity_registry().find(east)->health() == 100);

    // The narrow controller cannot reach an eastward target on its own.
    aims[0]->set_enabled(false);
    ticks(20);
    CHECK_FALSE(aims[1]->on_target());
    const auto stopped_shots = weapons[1]->shots_at_target;
    ticks(20);
    CHECK(weapons[1]->shots_at_target == stopped_shots);
    CHECK(observer->bone_world_forward(2).x < 0.5f);

    aims[0]->set_enabled(true);
    ticks(60);
    CHECK(aims[1]->on_target());
    const auto north = make_target(40, 60);
    weapons[1]->set_target_entity(north);
    ticks(60);
    CHECK(weapons[0]->target_entity_id == east);
    CHECK(weapons[1]->target_entity_id == north);
    CHECK_FALSE(aims[1]->on_target());
    CHECK(weapons[1]->shots_at_target == 0);
    // Sharing a bone is not permission to borrow another weapon's target
    // or on-target flag when the barrel cannot actually face our target.
    CHECK(observer->bone_world_forward(2).x > 0.95f);
}

TEST_CASE("Firing tolerance uses the desired angle beyond the mechanical arc",
          "[weapon][zero-damage-targeter][aim-tolerance]") {
    osc::sim::BoneData bones;
    osc::sim::BoneInfo yaw;
    yaw.name = "yaw";
    bones.bones.push_back(yaw);
    osc::sim::BoneInfo pitch;
    pitch.name = "pitch";
    pitch.parent_index = 0;
    bones.bones.push_back(pitch);
    osc::sim::Unit unit;
    unit.set_bone_data(&bones);
    unit.init_animated_bones();
    auto controller = std::make_unique<osc::sim::AimManipulator>();
    controller->set_yaw_bone(0);
    controller->set_pitch_bone(1);
    controller->set_firing_arc(-10, 10, 90, -10, 10, 90);
    auto* aim = controller.get();
    unit.add_manipulator(std::move(controller));
    bool pitch_axis = false;
    SECTION("yaw") {}
    SECTION("pitch") {
        pitch_axis = true;
    }
    constexpr float radians = 0.01745329252f;
    for (const float sign : {-1.0f, 1.0f}) {
        INFO("direction " << sign << " pitch axis " << pitch_axis);
        aim->set_heading_pitch(0, 0);
        for (const float desired_degrees : {11.5f, 13.0f}) {
            INFO("desired degrees " << desired_degrees);
            const float angle = sign * desired_degrees * radians;
            const float side = 20 * std::sin(angle);
            const float ahead = 20 * std::cos(angle);
            aim->set_target(pitch_axis ? osc::sim::Vector3{0, side, ahead}
                                       : osc::sim::Vector3{side, 0, ahead},
                            2 * radians);
            for (int tick = 0; tick < 20; ++tick) unit.tick_manipulators(0.1f, nullptr);
            CHECK(aim->on_target() == (desired_degrees == 11.5f));
            const float actual = pitch_axis ? aim->pitch() : aim->heading();
            CHECK(std::fabs(actual - sign * 10 * radians) < 1e-5f);
        }
    }
}
