#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "sim/entity_registry.hpp"
#include "sim/manipulator.hpp"
#include "sim/projectile.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

#include <memory>

namespace {

using osc::sim::CategoryExpr;
using osc::sim::EntityRegistry;
using osc::sim::Projectile;
using osc::sim::ProjectileBlueprintInfo;
using osc::sim::Unit;
using osc::sim::Weapon;

struct RecheckScene {
    EntityRegistry registry;
    Unit* owner = nullptr;
    Weapon* weapon = nullptr;

    RecheckScene() {
        registry.init_spatial_grid(128, 128);
        owner = &unit(0, 64, 64);
        owner->set_motion_type("RULEUMT_Land");
        auto gun = std::make_unique<Weapon>();
        gun->damage = 10;
        gun->max_range = 40;
        gun->target_check_period = 5;
        gun->always_recheck_target = true;
        gun->target_priorities = {CategoryExpr::name("TOP"), CategoryExpr::all()};
        weapon = gun.get();
        owner->add_weapon(std::move(gun));
    }

    Unit& unit(int army, float x, float z) {
        auto result = std::make_unique<Unit>();
        result->set_army(army);
        result->set_layer("Land");
        result->set_position({x, 10, z});
        result->set_max_health(100);
        result->set_health(100);
        auto* raw = result.get();
        registry.register_entity(std::move(result));
        return *raw;
    }

    Projectile& projectile(float z) {
        auto result = std::make_unique<Projectile>();
        result->set_army(1);
        result->set_position({64, 10, z});
        auto info = std::make_shared<ProjectileBlueprintInfo>();
        info->categories = {"ALLPROJECTILES", "TOP"};
        info->desired_shooter_cap = 1;
        result->set_blueprint_info(info);
        auto* raw = result.get();
        registry.register_entity(std::move(result));
        return *raw;
    }

    void retain(osc::u32 target) {
        weapon->set_target_entity(target);
        weapon->aim_spot_target = target;
        weapon->shots_at_target = 7;
    }

    void update() {
        // Exercise the ordinary public targeting path, without firing or
        // moving fixtures. No Lua callbacks or replacement source classes.
        weapon->fire_clock = 1000;
        weapon->update(*owner, registry, nullptr);
    }
};

} // namespace

TEST_CASE("A top-priority target survives a nearer or better-aligned equal-priority candidate",
          "[weapon][target-recheck]") {
    RecheckScene s;
    auto& candidate = s.unit(1, 64, 70); // Lower id as well as nearer.
    auto& current = s.unit(1, 64, 84);
    candidate.add_category("TOP");
    current.add_category("TOP");
    SECTION("The candidate is nearer") {}
    SECTION("The candidate has equal distance and a lower entity id") {
        candidate.set_position({84, 10, 64});
    }
    SECTION("The candidate is in arc while the retained slaved target is outside it") {
        s.weapon->heading_arc_range = 30;
        s.weapon->slaved_to_body = true;
        current.set_position({64, 10, 44});
        REQUIRE_FALSE(s.weapon->in_heading_arc(*s.owner, current.position()));
        REQUIRE(s.weapon->in_heading_arc(*s.owner, candidate.position()));
    }
    s.retain(current.entity_id());
    s.weapon->auto_initiate_attack_command = true;
    unsigned draws = 0;
    s.registry.sim_random().set_draw_hook(
        [](void* context, osc::u64) { ++*static_cast<unsigned*>(context); }, &draws);
    s.update();
    CHECK(s.weapon->target_entity_id == current.entity_id());
    CHECK(s.weapon->shots_at_target == 7);
    CHECK(s.weapon->target_check_clock == 4);
    CHECK(draws == 0);
}

TEST_CASE("A retained top-priority target is validated before its scheduled recheck",
          "[weapon][target-recheck]") {
    RecheckScene s;
    auto& current = s.unit(1, 64, 74);
    auto& replacement = s.unit(1, 64, 80);
    current.add_category("TOP");
    replacement.add_category("TOP");
    s.retain(current.entity_id());
    s.weapon->target_check_clock = 3;
    SECTION("Destroyed target") {
        current.mark_destroyed();
    }
    SECTION("Target made untargetable") {
        current.set_do_not_target(true);
    }
    SECTION("Target beyond tracking range") {
        current.set_position({64, 10, 120});
    }
    SECTION("Target cloaked without omni") {
        current.set_cloaked(true);
    }
    s.update();
    CHECK(s.weapon->target_entity_id == 0);
    CHECK(s.weapon->target_check_clock == 2);
    // Losing validity is immediate, but reacquisition still obeys the
    // existing interval; the shortcut must not turn either into a latch.
    s.update();
    s.update();
    CHECK(s.weapon->target_entity_id == 0);
    s.update();
    CHECK(s.weapon->target_entity_id == replacement.entity_id());
    CHECK(s.weapon->target_check_clock == 4);
}

TEST_CASE("Rechecks promote a lower-priority target to priority zero", "[weapon][target-recheck]") {
    RecheckScene s;
    auto& current = s.unit(1, 64, 70);
    auto& preferred = s.unit(1, 64, 90);
    preferred.add_category("TOP");
    s.retain(current.entity_id());
    REQUIRE(s.weapon->priority_of(current) == 1);
    REQUIRE(s.weapon->priority_of(preferred) == 0);
    s.update();
    CHECK(s.weapon->target_entity_id == preferred.entity_id());
    CHECK(s.weapon->target_check_clock == 4);
}

TEST_CASE("Empty priorities and BENIGN retain their existing recheck contracts",
          "[weapon][target-recheck]") {
    RecheckScene s;
    auto& candidate = s.unit(1, 64, 70);
    auto& current = s.unit(1, 64, 84);
    candidate.add_category("TOP");
    current.add_category("TOP");
    SECTION("An empty priority list treats all targets equally") {
        s.weapon->target_priorities.clear();
    }
    SECTION("An already held BENIGN target is not evicted by an equal priority") {
        current.add_category("BENIGN");
    }
    s.retain(current.entity_id());
    s.weapon->target_check_clock = 1;
    s.update();
    CHECK(s.weapon->target_entity_id == current.entity_id());
    CHECK(s.weapon->target_check_clock == 0);
    s.update();
    CHECK(s.weapon->target_entity_id == current.entity_id());
    CHECK(s.weapon->target_check_clock == 4);

    // A new acquisition still excludes BENIGN, regardless of priorities.
    candidate.add_category("BENIGN");
    current.add_category("BENIGN");
    s.weapon->set_target_entity(0);
    s.weapon->target_check_clock = 0;
    s.update();
    CHECK_FALSE(s.weapon->has_target());
}

TEST_CASE("Projectile rechecks still prune stale shooters at priority zero",
          "[weapon][target-recheck][projectile]") {
    RecheckScene s;
    s.weapon->targets_projectiles = true;
    auto& held = s.projectile(84);
    auto& other = s.projectile(70);
    s.retain(held.entity_id());
    held.shooters.emplace_back(s.owner->entity_id(), 0); // Still valid, fills its cap.
    other.shooters.emplace_back(999999, 0); // Stale entry must be pruned during the scan.
    REQUIRE(s.weapon->priority_of(held) == 0);
    s.update();
    CHECK(s.weapon->target_entity_id == held.entity_id());
    CHECK(held.shooters.size() == 1);
    CHECK(other.shooters.empty());
    CHECK(s.weapon->target_check_clock == 4);
}

TEST_CASE("Weapon acquisition filters enemies with the existing directed and null-sim contracts",
          "[weapon][target-recheck][alliance]") {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};
    for (const char* name : {"Owner", "Ally", "Neutral", "Enemy"}) sim.add_army(name, name);
    // ArmyBrain's relation is directed; SimState::set_alliance deliberately
    // sets both directions, so use the brain API for this asymmetric fixture.
    sim.get_army(0)->set_alliance(1, osc::sim::Alliance::Ally);
    sim.get_army(0)->set_alliance(2, osc::sim::Alliance::Neutral);
    sim.get_army(0)->set_alliance(3, osc::sim::Alliance::Enemy);
    sim.get_army(3)->set_alliance(0, osc::sim::Alliance::Ally);
    REQUIRE(sim.is_enemy(0, 3));
    REQUIRE(sim.is_ally(3, 0));

    RecheckScene s;
    auto& same_army = s.unit(0, 64, 64.5f);
    auto& no_army = s.unit(-1, 64, 65);
    auto& ally = s.unit(1, 64, 66);
    s.unit(2, 64, 68);
    auto& enemy = s.unit(3, 64, 70);
    auto& unknown_army = s.unit(99, 64, 72);
    s.weapon->target_priorities.clear();
    REQUIRE_FALSE(s.weapon->can_pick(*s.owner, no_army, &sim));
    REQUIRE_FALSE(s.weapon->can_pick(*s.owner, no_army, nullptr));
    const osc::sim::SimState* diplomacy = &sim;
    osc::u32 expected = enemy.entity_id();
    SECTION("Sim uses owner-to-target enemy, excluding ally and neutral") {}
    SECTION("Without a sim, different nonnegative army ids are enemies") {
        diplomacy = nullptr;
        expected = ally.entity_id();
    }
    SECTION("An unknown positive target army retains the brain's default enemy relation") {
        enemy.set_do_not_target(true);
        REQUIRE(sim.is_enemy(0, 99));
        expected = unknown_army.entity_id();
    }
    SECTION("A negative owner army has no enemies through the sim") {
        s.owner->set_army(-1);
        expected = 0;
    }
    SECTION("A negative owner without a sim still compares target army ids") {
        s.owner->set_army(-1);
        diplomacy = nullptr;
        expected = same_army.entity_id();
    }
    s.weapon->fire_clock = 1000;
    s.weapon->update(*s.owner, s.registry, nullptr, diplomacy);
    CHECK(s.weapon->target_entity_id == expected);
}
