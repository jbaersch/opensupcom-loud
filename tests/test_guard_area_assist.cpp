#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "blueprints/blueprint_store.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/sim_bindings.hpp"
#include "map/heightmap.hpp"
#include "map/terrain.hpp"
#include "renderer/input_handler.hpp"
#include "sim/manipulator.hpp"
#include "sim/prop.hpp"
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

namespace {

using osc::sim::CommandType;
using osc::sim::Unit;
using osc::sim::UnitCommand;
using osc::sim::Vector3;
constexpr float kY = 1000.0f / 128.0f;

struct AreaWorld {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store{lua.raw()};
    osc::sim::SimState sim{lua.raw(), &store};

    AreaWorld() {
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_sim_bindings(lua, sim);
        for (const char* name : {"ARMY_1", "ARMY_2", "ARMY_3"}) sim.add_army(name, name);
        sim.set_victory_condition("sandbox");
        sim.set_fog_of_war("none");
        sim.entity_registry().init_spatial_grid(128, 128);
        std::vector<osc::u16> heights(129 * 129, 1000);
        sim.set_terrain(std::make_unique<osc::map::Terrain>(
            osc::map::Heightmap(128, 128, 1.0f / 128.0f, std::move(heights)), 0.0f, false));
        sim.build_pathfinding_grid();
        REQUIRE(lua.do_string(R"(
            return {BlueprintId = 'area_site', Categories = {'STRUCTURE', 'LAND'},
                Footprint = {SizeX = 2, SizeZ = 2},
                Physics = {SkirtSizeX = 2, SkirtSizeZ = 2},
                Defense = {MaxHealth = 1000},
                Economy = {BuildTime = 100, BuildCostMass = 100, BuildCostEnergy = 1000},
                Enhancements = {AreaUpgrade = {Slot = 'Area', BuildTime = 100,
                    BuildCostMass = 100, BuildCostEnergy = 1000},
                    OtherUpgrade = {Slot = 'Area', BuildTime = 50,
                    BuildCostMass = 200, BuildCostEnergy = 2000}}}
        )")
                    .ok());
        store.register_blueprint(lua.raw(), osc::blueprints::BlueprintType::Unit,
                                 lua_gettop(lua.raw()));
        lua_pop(lua.raw(), 1);
        store.expose_to_lua(lua.raw());
        REQUIRE(lua.do_string(R"(
            AreaCompletions, AreaWorkBegins, AreaWorkEnds = 0, 0, 0
            AreaSiloBegins, AreaSiloEnds = 0, 0
            AreaClass = setmetatable({
                OnStopBeingBuilt = function(self) AreaCompletions = AreaCompletions + 1 end,
                OnWorkBegin = function(self, name)
                    AreaWorkBegins = AreaWorkBegins + 1
                    self.WorkItem = __blueprints.area_site.Enhancements[name]
                end,
                InheritWork = function(self, target) self.WorkItem = target.WorkItem end,
                ClearWork = function(self) self.WorkItem = nil end,
                OnWorkEnd = function(self)
                    AreaWorkEnds = AreaWorkEnds + 1
                    self:ClearWork()
                end,
                OnWorkFail = function(self) self:ClearWork() end,
                OnSiloBuildStart = function(self) AreaSiloBegins = AreaSiloBegins + 1 end,
                OnSiloBuildEnd = function(self) AreaSiloEnds = AreaSiloEnds + 1 end,
            }, {__index = moho.unit_methods})
            AreaClass.__index = AreaClass
        )")
                    .ok());
    }

    Unit* unit(float x, float z, int army = 0, float speed = 0) {
        auto unit = std::make_unique<Unit>();
        unit->set_unit_id("area_site");
        unit->set_blueprint_id("area_site");
        unit->set_army(army);
        unit->set_position({x, kY, z});
        unit->set_max_health(1000);
        unit->set_health(1000);
        unit->set_max_speed(speed);
        Unit::Drive drive;
        drive.max_accel = 50;
        unit->set_drive(drive);
        unit->set_size_xz(2, 2);
        unit->economy().production_active = true;
        unit->economy().storage_mass = 1e6;
        unit->economy().storage_energy = 1e6;
        auto* raw = unit.get();
        sim.entity_registry().register_entity(std::move(unit));
        lua_newtable(lua.raw());
        lua_pushstring(lua.raw(), "_c_object");
        lua_pushlightuserdata(lua.raw(), raw);
        lua_rawset(lua.raw(), -3);
        lua_pushstring(lua.raw(), "_c_sim_gen");
        lua_pushnumber(lua.raw(), osc::sim::SimState::sim_generation());
        lua_rawset(lua.raw(), -3);
        lua_getglobal(lua.raw(), "AreaClass");
        lua_setmetatable(lua.raw(), -2);
        raw->set_lua_table_ref(luaL_ref(lua.raw(), LUA_REGISTRYINDEX));
        raw->manage_resources(sim.get_army(army));
        sim.get_army(army)->economy().mass.stored = 1e6;
        sim.get_army(army)->economy().energy.stored = 1e6;
        return raw;
    }

    Unit* worker(float x, float z, bool repairs = true) {
        auto* raw = unit(x, z, 0, 5);
        raw->set_motion_type("RULEUMT_Land");
        raw->add_category("ENGINEER");
        raw->add_category("COMMAND");
        if (repairs) raw->add_category("REPAIR");
        raw->add_command_cap("RULEUCC_Guard");
        raw->set_build_rate(10);
        raw->set_max_build_distance(10);
        raw->set_guard_scan_radius(20);
        return raw;
    }

    static void guard(Unit& worker, Vector3 point, bool clear = true) {
        UnitCommand cmd;
        cmd.type = CommandType::Guard;
        cmd.target_pos = point;
        cmd.command_id = clear ? 91 : 92;
        worker.push_command(cmd, clear);
    }

    void start_work(Unit& target, bool missile, bool other = false) {
        target.set_build_rate(10);
        if (missile) {
            REQUIRE(lua.do_string(R"(
                __blueprints.area_missile = {
                    Economy = {BuildTime = 100, BuildCostMass = 100, BuildCostEnergy = 1000}}
                __blueprints.other_missile = {
                    Economy = {BuildTime = 50, BuildCostMass = 200, BuildCostEnergy = 2000}}
            )")
                        .ok());
            if (target.weapons().empty()) {
                auto launcher = std::make_unique<osc::sim::Weapon>();
                launcher->counted_projectile = true;
                launcher->nuke_weapon = true;
                launcher->max_projectile_storage = 5;
                target.add_weapon(std::move(launcher));
            }
            target.weapons().front()->projectile_bp_id = other ? "other_missile" : "area_missile";
            target.order_silo_build(true);
        } else {
            UnitCommand enhance;
            enhance.type = CommandType::Enhance;
            enhance.blueprint_id = other ? "OtherUpgrade" : "AreaUpgrade";
            target.push_command(enhance, true);
        }
    }

    void tick(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) sim.tick();
    }
};

} // namespace

TEST_CASE("The assist command button accepts bare ground and preserves a point guard",
          "[guard][area-assist][command-routing]") {
    AreaWorld world;
    auto* worker = world.worker(20, 20);
    osc::renderer::InputHandler input;
    input.set_player_army(0);
    input.set_selected({worker->entity_id()});
    osc::renderer::CommandMode mode;
    mode.mode = "order";
    mode.name = "RULEUCC_Guard";
    const auto issued = input.click_in_command_mode(world.sim, mode, 60, 60, false);
    REQUIRE(issued);
    CHECK(issued->type == "Guard");
    CHECK(issued->target_id == 0);
    CHECK(issued->position.x == 60);
    CHECK(issued->position.z == 60);
    world.tick(2);
    REQUIRE(worker->guard_order());
    CHECK(worker->guard_order()->target_id == 0);
    CHECK(worker->guard_order()->target_pos.x == 60);
    CHECK(worker->guard_order()->target_pos.z == 60);
}

TEST_CASE("A ground guard builds nearby unfinished allies then searches for more work",
          "[guard][area-assist][build]") {
    AreaWorld world;
    world.sim.set_alliance(0, 1, osc::sim::Alliance::Ally);
    auto* worker = world.worker(56, 60);
    auto* site = world.unit(60, 60, 1);
    site->set_fraction_complete(0.25f);
    site->set_is_being_built(true);
    site->set_health(250);
    auto* damaged = world.unit(63, 60, 1);
    damaged->set_health(100);
    AreaWorld::guard(*worker, {59, kY, 59});
    world.tick(8);
    REQUIRE(worker->build_target_id() == site->entity_id());
    CHECK(site->fraction_complete() > 0.25f);
    REQUIRE(worker->guard_order());
    CHECK(worker->guard_order()->target_id == 0);
    CHECK(worker->guard_order()->target_pos.x == 59);
    for (int i = 0; i < 300 && (site->is_being_built() || damaged->health() < 1000); ++i)
        world.tick();
    CHECK_FALSE(site->is_being_built());
    CHECK(site->army() == 1);
    CHECK(damaged->health() == 1000);
    CHECK(world.lua.do_string("assert(AreaCompletions == 1)").ok());
    REQUIRE(worker->guard_order());
    CHECK(worker->guard_order()->command_id == 91);
}

TEST_CASE("Point assist searches a box around the guard point and ranks by worker distance",
          "[guard][area-assist]") {
    AreaWorld world;
    auto* worker = world.worker(78, 79);
    auto* corner = world.unit(79, 79);
    corner->set_health(100);
    auto* middle = world.unit(61, 60);
    middle->set_fraction_complete(0.2f);
    middle->set_is_being_built(true);
    middle->set_health(200);
    middle->set_creation_tick(1000); // isolate selection from native unfinished-unit decay
    AreaWorld::guard(*worker, {60, kY, 60});
    world.tick(8);
    CHECK(worker->repair_target_id() == corner->entity_id());
    CHECK(corner->health() > 100);
    CHECK(middle->fraction_complete() == 0.2f);
}

TEST_CASE("Point assist rejects moving, airborne, enemy and out-of-box candidates",
          "[guard][area-assist]") {
    AreaWorld world;
    auto* worker = world.worker(77, 60);
    auto* valid = world.unit(70, 60);
    valid->set_health(100);
    auto* ignored = world.unit(78, 60);
    ignored->set_health(100);
    SECTION("moving") {
        ignored->set_max_speed(5);
        ignored->set_velocity({5, 0, 0});
        UnitCommand move;
        move.type = CommandType::Move;
        move.target_pos = {110, kY, 60};
        ignored->push_command(move, true);
    }
    SECTION("airborne") {
        ignored->set_layer("Air");
    }
    SECTION("enemy") {
        ignored->set_army(2);
    }
    SECTION("outside the point's search box") {
        ignored->set_position({81, kY, 60});
        ignored->place_now();
    }
    AreaWorld::guard(*worker, {60, kY, 60});
    world.tick(8);
    CHECK(worker->repair_target_id() == valid->entity_id());
    CHECK(valid->health() > 100);
    CHECK(ignored->health() == 100);
}

TEST_CASE("Point assist follows a builder's work focus beyond the search box",
          "[guard][area-assist]") {
    AreaWorld world;
    auto* worker = world.worker(90, 89);
    auto* source = world.unit(60, 60);
    auto* site = world.unit(90, 90);
    site->set_fraction_complete(0.25f);
    site->set_is_being_built(true);
    site->set_health(250);
    source->set_focus_entity_id(site->entity_id());
    AreaWorld::guard(*worker, {60, kY, 60});
    world.tick(8);
    CHECK(worker->build_target_id() == site->entity_id());
    CHECK(site->fraction_complete() > 0.25f);
    REQUIRE(worker->guard_order());
    CHECK(worker->guard_order()->target_pos.x == 60);
}

TEST_CASE(
    "Point assist contributes to healthy active work items and preserves their owner's lifecycle",
    "[guard][area-assist][area-work-item]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    auto* target = world.unit(60, 60);
    target->set_build_rate(10);
    bool missile = false;
    SECTION("enhancement") {
        UnitCommand enhance;
        enhance.type = CommandType::Enhance;
        enhance.blueprint_id = "AreaUpgrade";
        target->push_command(enhance, true);
    }
    SECTION("silo ammunition") {
        missile = true;
        REQUIRE(world.lua
                    .do_string(R"(
            __blueprints.area_missile = {
                Economy = {BuildTime = 100, BuildCostMass = 100, BuildCostEnergy = 1000}}
        )")
                    .ok());
        auto launcher = std::make_unique<osc::sim::Weapon>();
        launcher->counted_projectile = true;
        launcher->nuke_weapon = true;
        launcher->max_projectile_storage = 5;
        launcher->projectile_bp_id = "area_missile";
        target->add_weapon(std::move(launcher));
        target->order_silo_build(true);
    }
    AreaWorld::guard(*worker, {59, kY, 59});
    world.tick(8);
    REQUIRE((missile ? target->silo_building() : target->is_enhancing()));
    REQUIRE(target->health() == target->max_health());
    const double before = missile ? target->silo_build().progress : target->work_progress();
    world.tick();
    const double after = missile ? target->silo_build().progress : target->work_progress();
    CHECK(after - before == Catch::Approx(20.0 / 100.0 * 0.1));
    CHECK(worker->is_repairing());
    CHECK(world.sim.get_army(0)->economy().mass.requested == Catch::Approx(20));
    CHECK(world.sim.get_army(0)->economy().energy.requested == Catch::Approx(200));
    world.tick(150);
    CHECK_FALSE(target->silo_building());
    CHECK_FALSE(target->is_enhancing());
    if (missile) {
        CHECK(target->nuke_silo_ammo() == 1);
        CHECK(world.lua.do_string("assert(AreaSiloBegins == 1 and AreaSiloEnds == 1)").ok());
    } else {
        CHECK(target->has_enhancement("AreaUpgrade"));
        CHECK(world.lua.do_string("assert(AreaWorkBegins == 1 and AreaWorkEnds == 1)").ok());
    }
    REQUIRE(worker->guard_order());
    CHECK(worker->guard_order()->command_id == 91);
    CHECK(worker->health() == worker->max_health());
}

TEST_CASE("GetFocusUnit exposes a repair target, retires it on stop and preserves construction "
          "precedence",
          "[guard][area-assist][assist-focus]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    auto* target = world.unit(60, 60);
    target->set_health(100);
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, worker->lua_table_ref());
    lua_setglobal(world.lua.raw(), "AreaWorker");
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, target->lua_table_ref());
    lua_setglobal(world.lua.raw(), "AreaTarget");
    REQUIRE(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(AreaWorker) == nil)").ok());
    UnitCommand repair;
    repair.type = CommandType::Repair;
    repair.target_id = target->entity_id();
    repair.target_pos = target->position();
    worker->push_command(repair, true);
    world.tick(8);
    REQUIRE(worker->repair_target_id() == target->entity_id());
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(AreaWorker) == AreaTarget)")
              .ok());
    {
        auto ctx = world.sim.sim_context();

        ctx.L = world.lua.raw();

        worker->clear_commands(ctx);
    }
    world.tick(); // clearing the queue retires its repair on the next unit update
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(AreaWorker) == nil)").ok());

    worker->set_capture_target_id(target->entity_id());
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(AreaWorker) == AreaTarget)")
              .ok());
    worker->set_capture_target_id(0);
    target->set_is_being_built(true);
    target->set_fraction_complete(0.25f);
    target->set_health(250);
    worker->push_command(repair, true);
    world.tick(8);
    REQUIRE(worker->build_target_id() == target->entity_id());
    auto* captured = world.unit(65, 65, 1);
    worker->set_capture_target_id(captured->entity_id());
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(AreaWorker) == AreaTarget)")
              .ok());
    worker->set_capture_target_id(0);
    {
        auto ctx = world.sim.sim_context();

        ctx.L = world.lua.raw();

        worker->clear_commands(ctx);
    }
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(AreaWorker) == nil)").ok());
}

TEST_CASE("Repair heals damage before assisting an active work item",
          "[guard][area-assist][repair-priority]") {
    AreaWorld world;
    bool missile = true;
    bool shield = false;
    SECTION("a damaged silo") {}
    SECTION("a damaged upgrading unit") {
        missile = false;
    }
    SECTION("a silo with a damaged shield") {
        shield = true;
    }
    auto* target = world.unit(60, 60);
    auto* control = world.unit(90, 90);
    auto* worker = world.worker(56, 60);
    world.start_work(*target, missile);
    world.start_work(*control, missile);
    REQUIRE(world.lua
                .do_string(missile
                               ? "__blueprints.area_missile.Economy.BuildTime = 1000"
                               : "__blueprints.area_site.Enhancements.AreaUpgrade.BuildTime = 1000")
                .ok());
    world.tick(8);
    REQUIRE((missile ? target->silo_building() : target->is_enhancing()));
    Unit* damaged = target;
    if (shield) {
        damaged = world.unit(60, 60);
        target->add_category("SHIELD");
        target->set_focus_entity_id(damaged->entity_id());
        lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, target->lua_table_ref());
        lua_setglobal(world.lua.raw(), "RepairSubject");
        lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, damaged->lua_table_ref());
        lua_setglobal(world.lua.raw(), "RepairBubble");
        REQUIRE(world.lua
                    .do_string(R"(
            RepairSubject.ShieldIsOn = function() return true end
            RepairBubble.RegenRate = 10
        )")
                    .ok());
    }
    damaged->set_health(900);
    UnitCommand repair;
    repair.type = CommandType::Repair;
    repair.target_id = target->entity_id();
    worker->push_command(repair, true);
    const auto progress = [&](const Unit& unit) {
        return missile ? unit.silo_build().progress : unit.work_progress();
    };
    world.tick(6);
    CHECK(damaged->health() > 900);
    CHECK(progress(*target) == Catch::Approx(progress(*control)));
    world.tick(40);
    CHECK(damaged->health() == damaged->max_health());
    CHECK(worker->command_queue().empty());
    CHECK_FALSE(worker->is_repairing());
    CHECK(progress(*target) == Catch::Approx(progress(*control)));
}

TEST_CASE("Assisting work items respects helper stop, target cancellation, pause and replacement",
          "[guard][area-assist][area-work-item]") {
    const bool missile = GENERATE(false, true);
    CAPTURE(missile);
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    auto* target = world.unit(60, 60);
    world.start_work(*target, missile);
    AreaWorld::guard(*worker, {59, kY, 59});
    world.tick(8);
    REQUIRE(worker->is_repairing());
    const auto progress = [&] {
        return missile ? target->silo_build().progress : target->work_progress();
    };
    SECTION("stopping the helper leaves the owner's work and costs intact") {
        {
            auto ctx = world.sim.sim_context();

            ctx.L = world.lua.raw();

            worker->clear_commands(ctx);
        }
        const double before = progress();
        world.tick();
        CHECK(progress() - before == Catch::Approx(10.0 / 100.0 * 0.1));
        CHECK_FALSE(worker->is_repairing());
        CHECK(worker->economy().consumption_mass == 0);
        CHECK(worker->economy().consumption_energy == 0);
        // Army requests were collected before this tick's unit cleanup.
        world.tick();
        CHECK(world.sim.get_army(0)->economy().mass.requested == Catch::Approx(10));
        CHECK(world.sim.get_army(0)->economy().energy.requested == Catch::Approx(100));
        if (!missile) {
            lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, target->lua_table_ref());
            lua_setglobal(world.lua.raw(), "AreaOwner");
            lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, worker->lua_table_ref());
            lua_setglobal(world.lua.raw(), "AreaHelper");
            CHECK(world.lua
                      .do_string("assert(AreaOwner.WorkItem ~= nil and AreaHelper.WorkItem == nil)")
                      .ok());
        }
    }
    SECTION("cancelling the owner's work releases the helper but keeps its area order") {
        {
            auto ctx = world.sim.sim_context();

            ctx.L = world.lua.raw();

            target->clear_commands(ctx);
        }
        target->stop_silo_build();
        world.tick(2);
        CHECK_FALSE(worker->is_repairing());
        CHECK_FALSE(target->is_enhancing());
        CHECK_FALSE(target->silo_building());
        REQUIRE(worker->guard_order());
        CHECK(worker->guard_order()->command_id == 91);
        CHECK(world.sim.get_army(0)->economy().mass.requested == 0);
        CHECK(world.sim.get_army(0)->economy().energy.requested == 0);
    }
    SECTION("a paused owner receives no extra progress and the helper resumes after unpause") {
        target->set_paused(true);
        const double before = progress();
        world.tick(3);
        CHECK(progress() == before);
        CHECK_FALSE(worker->is_repairing());
        REQUIRE(worker->guard_order());
        target->set_paused(false);
        world.tick(8);
        REQUIRE(worker->is_repairing());
        const double resumed = progress();
        world.tick();
        CHECK(progress() - resumed == Catch::Approx(20.0 / 100.0 * 0.1));
    }
    SECTION("a different work item uses its own time and costs") {
        {
            auto ctx = world.sim.sim_context();

            ctx.L = world.lua.raw();

            target->clear_commands(ctx);
        }
        target->stop_silo_build();
        world.start_work(*target, missile, true);
        world.tick(8);
        REQUIRE(worker->is_repairing());
        const double before = progress();
        world.tick();
        CHECK(progress() - before == Catch::Approx(20.0 / 50.0 * 0.1));
        CHECK(world.sim.get_army(0)->economy().mass.requested == Catch::Approx(80));
        CHECK(world.sim.get_army(0)->economy().energy.requested == Catch::Approx(800));
    }
}

TEST_CASE("Area assistance requires REPAIR or REBUILDER capability and a rebuilder's queued anchor",
          "[guard][area-assist]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60, false);
    auto* site = world.unit(60, 60);
    site->set_health(100);
    bool expected = false;
    SECTION("build rate and ENGINEER alone do not grant area assistance") {}
    SECTION("rebuilder requires a guard command at the candidate position") {
        worker->add_category("REBUILDER");
    }
    SECTION("rebuilder's additional guard command selects the corresponding site") {
        worker->add_category("REBUILDER");
        AreaWorld::guard(*worker, site->position(), false);
        expected = true;
    }
    AreaWorld::guard(*worker, {59, kY, 59}, false);
    // Keep the first anchor at the head and a candidate-position anchor
    // behind it in the rebuilder section.
    if (expected) {
        {
            auto ctx = world.sim.sim_context();

            ctx.L = world.lua.raw();

            worker->clear_commands(ctx);
        }
        AreaWorld::guard(*worker, {59, kY, 59});
        AreaWorld::guard(*worker, site->position(), false);
    }
    world.tick(8);
    CHECK((worker->repair_target_id() == site->entity_id()) == expected);
    CHECK((site->health() > 100) == expected);
}

TEST_CASE("Enhancement assistance preserves the helper's source-computed costs",
          "[guard][area-assist][area-work-item]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    auto* target = world.unit(60, 60);
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, worker->lua_table_ref());
    lua_setglobal(world.lua.raw(), "CostHelper");
    REQUIRE(world.lua
                .do_string(R"(
        function CostHelper:OnStartBuild(target, order)
            assert(self.WorkItem ~= nil)
            -- This represents this helper's own source modifiers and upkeep.
            self:SetConsumptionPerSecondMass(7)
            self:SetConsumptionPerSecondEnergy(11)
        end
    )")
                .ok());
    world.start_work(*target, false);
    world.tick();
    REQUIRE(target->is_enhancing());
    // The subject's drain is deliberately unrelated to this helper's work.
    target->economy().consumption_mass = 500;
    target->economy().consumption_energy = 5000;
    AreaWorld::guard(*worker, {59, kY, 59});
    world.tick(8);
    REQUIRE(worker->is_repairing());
    CHECK(worker->economy().consumption_mass == 7);
    CHECK(worker->economy().consumption_energy == 11);
    world.tick(3);
    CHECK(worker->economy().consumption_mass == 7);
    CHECK(worker->economy().consumption_energy == 11);
}

TEST_CASE("An InheritWork callback replacing the command does not start the retired repair",
          "[guard][area-assist][area-work-item]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    auto* target = world.unit(60, 60);
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, worker->lua_table_ref());
    lua_setglobal(world.lua.raw(), "ReentrantHelper");
    REQUIRE(world.lua
                .do_string(R"(
        RetiredRepairStarts = 0
        function ReentrantHelper:InheritWork(target)
            self:Stop()
            IssueMove({self}, {20, 0, 20})
        end
        function ReentrantHelper:OnStartBuild()
            RetiredRepairStarts = RetiredRepairStarts + 1
        end
    )")
                .ok());
    worker->add_command_cap("RULEUCC_Move");
    world.start_work(*target, false);
    AreaWorld::guard(*worker, {59, kY, 59});
    world.tick(8);
    CHECK_FALSE(worker->is_repairing());
    CHECK(world.lua.do_string("assert(RetiredRepairStarts == 0)").ok());
    REQUIRE_FALSE(worker->command_queue().empty());
    CHECK(worker->command_queue().front().type == CommandType::Move);
}

TEST_CASE("GetFocusUnit exposes an active unit reclaim and clears after a stopped reclaim",
          "[guard][area-assist][assist-focus][reclaim-focus]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    worker->add_category("RECLAIM");
    auto* target = world.unit(60, 60);
    target->add_category("RECLAIMABLE");
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, worker->lua_table_ref());
    lua_setglobal(world.lua.raw(), "ReclaimWorker");
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, target->lua_table_ref());
    lua_setglobal(world.lua.raw(), "ReclaimUnit");
    REQUIRE(world.lua
                .do_string(R"(
        function ReclaimWorker:GetReclaimCosts(target) return 100, 0, 100000 end
    )")
                .ok());
    UnitCommand reclaim;
    reclaim.type = CommandType::Reclaim;
    reclaim.target_id = target->entity_id();
    reclaim.target_pos = target->position();
    worker->push_command(reclaim, true);
    for (int tick = 0; tick < 8 && !worker->is_reclaiming(); ++tick) world.tick();
    REQUIRE(worker->is_reclaiming());
    CHECK(
        world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(ReclaimWorker) == ReclaimUnit)")
            .ok());
    UnitCommand stop;
    stop.type = CommandType::Stop;
    worker->push_command(stop, true);
    world.tick();
    CHECK_FALSE(worker->is_reclaiming());
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(ReclaimWorker) == nil)").ok());
}

TEST_CASE("GetFocusUnit never returns a reclaimed prop even though it has a live Lua handle",
          "[guard][area-assist][assist-focus][reclaim-focus]") {
    AreaWorld world;
    auto* worker = world.worker(56, 60);
    worker->add_category("RECLAIM");
    auto prop = std::make_unique<osc::sim::Prop>();
    prop->set_position({60, kY, 60});
    prop->reclaim_mass_max = 100000;
    prop->reclaim_energy_max = 100000;
    prop->reclaimable_category = true;
    auto* target = prop.get();
    world.sim.entity_registry().register_entity(std::move(prop));
    lua_newtable(world.lua.raw());
    lua_pushstring(world.lua.raw(), "_c_object");
    lua_pushlightuserdata(world.lua.raw(), target);
    lua_rawset(world.lua.raw(), -3);
    target->set_lua_table_ref(luaL_ref(world.lua.raw(), LUA_REGISTRYINDEX));
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, worker->lua_table_ref());
    lua_setglobal(world.lua.raw(), "ReclaimWorker");
    REQUIRE(world.lua
                .do_string(R"(
        function ReclaimWorker:GetReclaimCosts(target) return 100, 0, 100000 end
    )")
                .ok());
    UnitCommand reclaim;
    reclaim.type = CommandType::Reclaim;
    reclaim.target_id = target->entity_id();
    reclaim.target_pos = target->position();
    worker->push_command(reclaim, true);
    for (int tick = 0; tick < 8 && !worker->is_reclaiming(); ++tick) world.tick();
    REQUIRE(worker->is_reclaiming());
    CHECK(world.lua.do_string("assert(moho.unit_methods.GetFocusUnit(ReclaimWorker) == nil)").ok());
}
