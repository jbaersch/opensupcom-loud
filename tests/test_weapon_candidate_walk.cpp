#include <catch2/catch_test_macros.hpp>

#include "sim/entity_registry.hpp"
#include "sim/manipulator.hpp"
#include "sim/projectile.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace {

using osc::u32;
using namespace osc::sim;

Unit& add_unit(EntityRegistry& registry, int army, float x, float z) {
    auto unit = std::make_unique<Unit>();
    unit->set_army(army);
    unit->set_layer("Land");
    unit->set_motion_type("RULEUMT_Land");
    unit->set_position({x, 10, z});
    unit->set_max_health(100);
    unit->set_health(100);
    auto* result = unit.get();
    registry.register_entity(std::move(unit));
    return *result;
}

// The previous sorted candidate reduction, intentionally independent of
// visitor order and of the new explicit id tie in production.
u32 sorted_reference(const Unit& owner, const Weapon& weapon, const EntityRegistry& registry) {
    const auto candidates =
        registry.units_in_radius(owner.position().x, owner.position().z,
                                 weapon.max_range * std::max(1.0f, weapon.tracking_radius));
    u32 best = 0;
    int priority = 0;
    bool arc = false;
    float distance = 0;
    for (const Entity* candidate : candidates) {
        if (!weapon.can_target(owner, *candidate, nullptr) ||
            static_cast<const Unit*>(candidate)->has_category("BENIGN"))
            continue;
        const int rank = weapon.priority_of(*candidate);
        if (rank < 0) continue;
        const float dx = candidate->position().x - owner.position().x;
        const float dz = candidate->position().z - owner.position().z;
        const float d2 = dx * dx + dz * dz;
        const bool in_arc = weapon.in_heading_arc(owner, candidate->position());
        if (!best || rank < priority ||
            (rank == priority && ((in_arc && !arc) || (in_arc == arc && d2 < distance)))) {
            best = candidate->entity_id();
            priority = rank;
            arc = in_arc;
            distance = d2;
        }
    }
    return best;
}

} // namespace

TEST_CASE("Unit target reduction matches the sorted reference after cell relinking",
          "[weapon][candidate-walk]") {
    EntityRegistry registry;
    bool grid = false;
    SECTION("Without spatial grid") {}
    SECTION("With spatial grid") {
        grid = true;
        registry.init_spatial_grid(256, 256);
    }
    Unit& owner = add_unit(registry, 0, 128, 128);
    Weapon weapon;
    weapon.damage = 1;
    weapon.max_range = 64;
    weapon.slaved_to_body = true;
    weapon.heading_arc_range = 35;
    weapon.target_priorities = {CategoryExpr::name("PREFERRED"), CategoryExpr::all()};
    std::vector<Unit*> units;
    for (int i = 0; i < 60; ++i) {
        auto& unit =
            add_unit(registry, i % 3 ? 1 : 0, 128.0f + static_cast<float>((i * 17) % 105 - 52),
                     128.0f + static_cast<float>((i * 29) % 105 - 52));
        if (i % 4 == 0) unit.add_category("PREFERRED");
        if (i % 7 == 0) unit.add_category("BENIGN");
        if (i % 11 == 0) unit.set_cloaked(true);
        units.push_back(&unit);
    }
    if (!grid) {
        add_unit(registry, 1, std::numeric_limits<float>::quiet_NaN(), 128);
        add_unit(registry, 1, std::numeric_limits<float>::infinity(), 128);
    }
    // Projectiles do not enter the unit-only candidate walk.
    registry.register_entity(std::make_unique<Projectile>());
    for (int step = 0; step < 30; ++step) {
        INFO("grid " << grid << " step " << step);
        // Re-enter cells in reverse id order, forcing a traversal that is
        // neither creation order nor nearest order.
        for (auto it = units.rbegin(); it != units.rend(); ++it) {
            if (!registry.find((*it)->entity_id())) continue;
            auto at = (*it)->position();
            at.x += (step % 2 ? 1.0f : -1.0f) * 9;
            (*it)->set_position(at);
        }
        if (step == 5) units[4]->mark_destroyed();
        if (step == 10) registry.unregister_entity(units[8]->entity_id());
        if (step == 12) units[12]->begin_dying();
        const u32 expected = sorted_reference(owner, weapon, registry);
        weapon.set_target_entity(0);
        weapon.target_check_clock = 0;
        weapon.fire_clock = 10000;
        weapon.update(owner, registry, nullptr);
        CHECK(weapon.target_entity_id == expected);
        if (step == 20 && grid) registry.init_spatial_grid(256, 256);
    }
}

TEST_CASE("Equal unit candidates choose the lowest id across cells and infinite distances",
          "[weapon][candidate-walk]") {
    EntityRegistry registry;
    bool huge = false;
    SECTION("Equal finite distances, higher id visited first") {
        registry.init_spatial_grid(256, 256);
    }
    SECTION("Squared finite coordinate distances overflow without a spatial grid") {
        huge = true;
    }
    auto& owner = add_unit(registry, 0, 128, 128);
    auto& lower = add_unit(registry, 1, 160, 128);
    auto& higher = add_unit(registry, 1, 96, 128);
    Weapon weapon;
    weapon.damage = 1;
    weapon.max_range = 40;
    if (huge) {
        weapon.max_range = 3.0e20f;
        lower.set_position({2.0e20f, 10, 128});
        higher.set_position({-2.0e20f, 10, 128});
    }
    weapon.fire_clock = 10000;
    CHECK(sorted_reference(owner, weapon, registry) == lower.entity_id());
    weapon.update(owner, registry, nullptr);
    CHECK(weapon.target_entity_id == lower.entity_id());
}

TEST_CASE("Unit and projectile target scans keep separate id ties in a mixed registry",
          "[weapon][candidate-walk][projectile]") {
    EntityRegistry registry;
    registry.init_spatial_grid(256, 256);
    auto& owner = add_unit(registry, 0, 128, 128);
    auto& lower_unit = add_unit(registry, 1, 160, 128);
    const auto add_projectile = [&](float x) -> Projectile& {
        auto projectile = std::make_unique<Projectile>();
        projectile->set_army(1);
        projectile->set_position({x, 10, 128});
        auto info = std::make_shared<ProjectileBlueprintInfo>();
        info->desired_shooter_cap = 1;
        projectile->set_blueprint_info(std::move(info));
        auto* result = projectile.get();
        registry.register_entity(std::move(projectile));
        return *result;
    };
    auto& lower_projectile = add_projectile(160);
    add_unit(registry, 1, 96, 128); // Higher unit id, earlier spatial cell.
    auto& higher_projectile = add_projectile(96);
    lower_projectile.shooters.emplace_back(999998, 0);
    higher_projectile.shooters.emplace_back(999999, 0);
    Weapon weapon;
    weapon.damage = 1;
    weapon.max_range = 40;
    weapon.fire_clock = 10000;
    SECTION("Ordinary weapon ignores the projectile ids and shooter lists") {
        weapon.update(owner, registry, nullptr);
        CHECK(weapon.target_entity_id == lower_unit.entity_id());
        CHECK(lower_projectile.shooters.size() == 1);
        CHECK(higher_projectile.shooters.size() == 1);
    }
    SECTION("Projectile weapon keeps canonical id selection and prunes both shooter lists") {
        weapon.targets_projectiles = true;
        weapon.update(owner, registry, nullptr);
        CHECK(weapon.target_entity_id == lower_projectile.entity_id());
        CHECK(lower_projectile.shooters.size() == 1); // Newly assigned owner.
        CHECK(lower_projectile.shooters.front().first == owner.entity_id());
        CHECK(higher_projectile.shooters.empty());
    }
}
