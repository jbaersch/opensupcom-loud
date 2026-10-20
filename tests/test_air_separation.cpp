#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "sim/air_combat.hpp"
#include "sim/manipulator.hpp"
#include "sim/shield.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"
#include "sim/weapon.hpp"

#include <cmath>
#include <memory>

namespace {

using osc::sim::Unit;
using osc::sim::Vector3;

struct AirWorld {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};

    AirWorld() {
        sim.add_army("ARMY_1", "Aircraft");
        sim.set_victory_condition("sandbox");
        sim.entity_registry().init_spatial_grid(128, 128);
        // The test inserts aircraft into an already running world. Their
        // default flight timestamp must not look like last tick's flight.
        sim.tick();
        sim.tick();
    }

    Unit* gunship(Vector3 at, bool attack = true) {
        auto unit = std::make_unique<Unit>();
        unit->set_army(0);
        unit->set_position(at);
        unit->set_max_health(100);
        unit->set_health(100);
        unit->set_motion_type("RULEUMT_Air");
        unit->set_layer("Air");
        unit->set_max_airspeed(12);
        unit->set_elevation_target(10);
        unit->set_current_altitude(10);
        unit->add_category("AIR");
        unit->add_category("MOBILE");
        osc::sim::AirCombatRules rules;
        rules.attack_elevation = 10;
        rules.circling_min_airspeed = 3;
        rules.circling_radius_min = rules.circling_radius_max = 0.8f;
        rules.circling_elevation_ratio = 0;
        rules.circling_dir_change = false;
        unit->set_air_combat_rules(rules);
        auto gun = std::make_unique<osc::sim::Weapon>();
        gun->max_range = 30;
        gun->damage = 10;
        gun->fire_clock = 1000000;
        unit->add_weapon(std::move(gun));
        auto* raw = unit.get();
        sim.entity_registry().register_entity(std::move(unit));
        if (attack) {
            osc::sim::UnitCommand cmd;
            cmd.type = osc::sim::CommandType::Attack;
            cmd.target_pos = {32, 0, 32};
            raw->push_command(cmd, true);
        }
        return raw;
    }
};

float distance(Vector3 a, Vector3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

void check_circling(const Unit& unit) {
    REQUIRE(unit.air_combat().flying);
    CHECK_FALSE(unit.navigator().is_moving());
    CHECK(std::hypot(unit.air_velocity().x, unit.air_velocity().z) > 0);
}

} // namespace

TEST_CASE("Nearby attacking gunships circle without pushing each other away",
          "[air][air-separation]") {
    AirWorld alone;
    auto* reference = alone.gunship({8, 10, 32});
    alone.gunship({80, 10, 80}, false);
    alone.sim.tick();
    check_circling(*reference);

    AirWorld together;
    auto* first = together.gunship({8, 10, 32});
    auto* second = together.gunship({8.5f, 10, 32});
    together.sim.tick();
    check_circling(*first);
    check_circling(*second);
    // Moho aircraft pass through one another in flight (upstream #693).
    // A neighbor must not change the flight controller's displacement.
    CHECK(distance(first->position(), reference->position()) == 0);
    CHECK(first->position().y == reference->position().y);
}

TEST_CASE("Coincident aircraft retain their flight path deterministically",
          "[air][air-separation]") {
    AirWorld reference;
    auto* flight = reference.gunship({8, 10, 32});
    reference.sim.tick();
    const Vector3 after_flight = flight->position();

    const auto run = [&] {
        AirWorld world;
        auto* first = world.gunship({8, 10, 32});
        // Place the neighbor exactly where the first aircraft's controller
        // finishes this tick, exercising exact coincidence.
        auto* parked = world.gunship(after_flight, false);
        parked->set_immobile(true);
        world.sim.tick();
        check_circling(*first);
        const Vector3 at = first->position();
        CHECK(std::isfinite(at.x));
        CHECK(std::isfinite(at.y));
        CHECK(std::isfinite(at.z));
        // No artificial entity-id displacement is added to the flight.
        CHECK(at.x == after_flight.x);
        CHECK(at.z == after_flight.z);
        CHECK(parked->position().x == after_flight.x);
        CHECK(parked->position().z == after_flight.z);
        return world.sim.compute_sync_checksum();
    };
    CHECK(run() == run());
}

TEST_CASE("Air separation leaves a navigation-free winged guard approach to its flight controller",
          "[air][air-separation]") {
    const auto run = [](bool neighbor) {
        AirWorld world;
        auto* charge = world.gunship({80, 10, 80}, false);
        charge->set_is_being_built(true);
        auto* guard = world.gunship({8, 10, 32}, false);
        osc::sim::UnitCommand cmd;
        cmd.type = osc::sim::CommandType::Guard;
        cmd.target_id = charge->entity_id();
        cmd.target_pos = charge->position();
        guard->push_command(cmd, true);
        if (neighbor) {
            auto* parked = world.gunship({8.5f, 10, 32}, false);
            parked->set_immobile(true);
        }
        world.sim.tick();
        REQUIRE(osc::sim::flies_winged_on_guard(*guard));
        CHECK_FALSE(guard->navigator().is_moving());
        REQUIRE(std::hypot(guard->air_velocity().x, guard->air_velocity().z) > 0);
        return guard->position();
    };
    const Vector3 alone = run(false);
    const Vector3 together = run(true);
    CHECK(together.x == alone.x);
    CHECK(together.y == alone.y);
    CHECK(together.z == alone.z);
}

TEST_CASE("Air avoidance does not move parked, carried or unfinished aircraft",
          "[air][air-separation]") {
    AirWorld world;
    auto* first = world.gunship({8, 10, 32}, false);
    auto* other = world.gunship({8.5f, 10, 32}, false);
    // A stale display-speed value does not prove this tick performed flight.
    first->set_current_airspeed(12);
    other->set_current_airspeed(12);
    SECTION("parked") {}
    SECTION("being built") {
        first->set_is_being_built(true);
        other->set_is_being_built(true);
    }
    SECTION("carried") {
        first->set_transport_id(other->entity_id());
        other->add_cargo(first->entity_id());
        // A carried unit follows its carrier; set their initial coordinates
        // alike so native attachment motion cannot masquerade as avoidance.
        first->set_position(other->position());
        first->place_now();
    }
    const Vector3 a = first->position();
    const Vector3 b = other->position();
    world.sim.tick();
    CHECK(first->position().x == a.x);
    CHECK(first->position().z == a.z);
    CHECK(other->position().x == b.x);
    CHECK(other->position().z == b.z);
}
