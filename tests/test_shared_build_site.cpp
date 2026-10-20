#include <catch2/catch_approx.hpp>
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

#include <memory>
#include <string>
#include <vector>

namespace {

using osc::sim::CommandType;
using osc::sim::Unit;
using osc::sim::UnitCommand;

struct SharedBuildWorld {
    osc::lua::LuaState lua;
    osc::blueprints::BlueprintStore store{lua.raw()};
    osc::sim::SimState sim{lua.raw(), &store};

    explicit SharedBuildWorld(bool spatial_grid = true) {
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_sim_bindings(lua, sim);
        sim.add_army("ARMY_1", "Builders");
        sim.add_army("ARMY_2", "Other army");
        sim.set_victory_condition("sandbox");
        if (spatial_grid) sim.entity_registry().init_spatial_grid(512, 512);
        for (const char* id : {"test_site", "other_site"}) {
            REQUIRE(lua.do_string(std::string("return {BlueprintId = '") + id + R"(',
                Categories = {'STRUCTURE', 'LAND'},
                Footprint = {SizeX = 2, SizeZ = 2},
                Physics = {SkirtSizeX = 2, SkirtSizeZ = 2},
                Defense = {MaxHealth = 1000},
                Economy = {BuildTime = 1000, BuildCostMass = 100, BuildCostEnergy = 1000}}
            )")
                        .ok());
            store.register_blueprint(lua.raw(), osc::blueprints::BlueprintType::Unit,
                                     lua_gettop(lua.raw()));
            lua_pop(lua.raw(), 1);
        }
        store.expose_to_lua(lua.raw());
        REQUIRE(lua.do_string(R"(
            SitesCreated, SitesCompleted = 0, 0
            BuilderStarts, BuilderStops, BuilderFailures = 0, 0, 0
            SiteClass = setmetatable({
                OnCreate = function(self) SitesCreated = SitesCreated + 1 end,
                OnStopBeingBuilt = function(self) SitesCompleted = SitesCompleted + 1 end,
            }, {__index = moho.unit_methods})
            SiteClass.__index = SiteClass
            BuilderClass = setmetatable({
                OnStartBuild = function(self) BuilderStarts = BuilderStarts + 1 end,
                OnStopBuild = function(self) BuilderStops = BuilderStops + 1 end,
                OnFailedToBuild = function(self) BuilderFailures = BuilderFailures + 1 end,
            }, {__index = moho.unit_methods})
            BuilderClass.__index = BuilderClass
        )")
                    .ok());
        lua_pushstring(lua.raw(), "__osc_unit_script_classes");
        lua_newtable(lua.raw());
        for (const char* id : {"test_site", "other_site"}) {
            lua_pushstring(lua.raw(), id);
            lua_getglobal(lua.raw(), "SiteClass");
            lua_rawset(lua.raw(), -3);
        }
        lua_rawset(lua.raw(), LUA_REGISTRYINDEX);
    }

    Unit* builder(float rate, float x, int army = 0) {
        auto unit = std::make_unique<Unit>();
        unit->set_army(army);
        unit->set_position({x, 0, 20});
        unit->set_build_rate(rate);
        unit->set_max_build_distance(50);
        unit->add_category("ENGINEER");
        unit->add_category("REPAIR");
        unit->economy().production_active = true;
        unit->economy().storage_mass = 1e6;
        unit->economy().storage_energy = 1e6;
        auto* raw = unit.get();
        sim.entity_registry().register_entity(std::move(unit));
        lua_newtable(lua.raw());
        const int table = lua_gettop(lua.raw());
        lua_pushstring(lua.raw(), "_c_object");
        lua_pushlightuserdata(lua.raw(), raw);
        lua_rawset(lua.raw(), table);
        lua_pushstring(lua.raw(), "_c_sim_gen");
        lua_pushnumber(lua.raw(), osc::sim::SimState::sim_generation());
        lua_rawset(lua.raw(), table);
        lua_getglobal(lua.raw(), "BuilderClass");
        lua_setmetatable(lua.raw(), table);
        raw->set_lua_table_ref(luaL_ref(lua.raw(), LUA_REGISTRYINDEX));
        raw->manage_resources(sim.get_army(army));
        sim.get_army(army)->economy().mass.stored = 1e6;
        sim.get_army(army)->economy().energy.stored = 1e6;
        return raw;
    }

    void build(const std::vector<Unit*>& builders, bool clear = true, float x = 20,
               const char* blueprint = "test_site") {
        UnitCommand cmd;
        cmd.type = CommandType::BuildMobile;
        cmd.blueprint_id = blueprint;
        cmd.target_pos = {x, 0, 20};
        std::vector<osc::u32> ids;
        ids.reserve(builders.size());
        for (const auto* builder : builders) ids.push_back(builder->entity_id());
        sim.route_player_command(ids, cmd, clear);
    }

    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) sim.tick();
    }

    std::vector<Unit*> sites() {
        std::vector<Unit*> result;
        sim.entity_registry().for_each_unit([&](osc::sim::Entity& e) {
            if (e.blueprint_id() == "test_site" || e.blueprint_id() == "other_site")
                result.push_back(static_cast<Unit*>(&e));
        });
        return result;
    }
};

} // namespace

TEST_CASE("A group mobile build shares one site and sums work and resource requests",
          "[build][shared-site]") {
    bool spatial_grid = false;
    SECTION("same-tick insertion into the spatial grid") {
        spatial_grid = true;
    }
    SECTION("linear query fallback") {}
    SharedBuildWorld world(spatial_grid);
    auto* first = world.builder(10, 12);
    auto* second = world.builder(20, 14);
    auto* third = world.builder(30, 16);
    world.build({first, second, third});
    world.tick(8);
    const auto sites = world.sites();
    REQUIRE(sites.size() == 1);
    auto* site = sites[0];
    for (const auto* builder : {first, second, third})
        CHECK(builder->build_target_id() == site->entity_id());
    const float before = site->fraction_complete();
    world.tick();
    CHECK(site->fraction_complete() - before == Catch::Approx(60.0f / 1000.0f * 0.1f));
    CHECK(world.sim.get_army(0)->economy().mass.requested == Catch::Approx(6));
    CHECK(world.sim.get_army(0)->economy().energy.requested == Catch::Approx(60));
    site->set_fraction_complete(0.995f);
    world.tick(3);
    CHECK_FALSE(site->is_being_built());
    CHECK(world.lua.do_string("assert(SitesCreated == 1 and SitesCompleted == 1)").ok());
    for (const auto* builder : {first, second, third}) {
        CHECK_FALSE(builder->is_building());
        CHECK(builder->command_queue().empty());
    }
}

TEST_CASE("Shared site helpers survive the primary builder stopping and retain queued builds",
          "[build][shared-site]") {
    SharedBuildWorld world;
    auto* first = world.builder(10, 12);
    auto* helper = world.builder(20, 14);
    world.build({first, helper});
    world.build({helper}, false, 30);
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* site = world.sites()[0];
    {
        auto ctx = world.sim.sim_context();

        ctx.L = world.lua.raw();

        first->clear_commands(ctx);
    }
    world.tick();
    const float before = site->fraction_complete();
    world.tick();
    CHECK(site->fraction_complete() - before == Catch::Approx(20.0f / 1000.0f * 0.1f));
    site->set_fraction_complete(0.999f);
    world.tick(6);
    REQUIRE(world.sites().size() == 2);
    CHECK_FALSE(site->is_being_built());
    CHECK(helper->is_building());
    CHECK(helper->build_target_id() != site->entity_id());
    CHECK(world.lua.do_string("assert(SitesCreated == 2 and SitesCompleted == 1)").ok());
}

TEST_CASE("Stopping a shared helper removes only its contribution", "[build][shared-site]") {
    SharedBuildWorld world;
    auto* first = world.builder(10, 12);
    auto* helper = world.builder(20, 14);
    world.build({first, helper});
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* site = world.sites()[0];
    {
        auto ctx = world.sim.sim_context();

        ctx.L = world.lua.raw();

        helper->clear_commands(ctx);
    }
    world.tick();
    const float before = site->fraction_complete();
    world.tick();
    CHECK(site->fraction_complete() - before == Catch::Approx(10.0f / 1000.0f * 0.1f));
    CHECK_FALSE(helper->is_building());
    CHECK(world.sites().size() == 1);
}

TEST_CASE("Allied shared builders pay their own costs and complete the original owner's site once",
          "[build][shared-site]") {
    SharedBuildWorld world;
    world.sim.set_alliance(0, 1, osc::sim::Alliance::Ally);
    auto* first = world.builder(10, 12, 1);
    auto* helper = world.builder(20, 14, 0);
    world.build({first});
    world.build({helper});
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* site = world.sites()[0];
    REQUIRE(site->army() == 1);
    const float before = site->fraction_complete();
    world.tick();
    CHECK(site->fraction_complete() - before == Catch::Approx(30.0f / 1000.0f * 0.1f));
    CHECK(world.sim.get_army(1)->economy().mass.requested == Catch::Approx(1));
    CHECK(world.sim.get_army(1)->economy().energy.requested == Catch::Approx(10));
    CHECK(world.sim.get_army(0)->economy().mass.requested == Catch::Approx(2));
    CHECK(world.sim.get_army(0)->economy().energy.requested == Catch::Approx(20));
    site->set_fraction_complete(0.999f);
    world.tick(3);
    CHECK_FALSE(site->is_being_built());
    CHECK(site->army() == 1);
    CHECK(first->command_queue().empty());
    CHECK(helper->command_queue().empty());
    CHECK(world.lua.do_string("assert(SitesCreated == 1 and SitesCompleted == 1)").ok());
}

TEST_CASE("A shared mobile build ends on completion even if its finished site is damaged",
          "[build][shared-site]") {
    SharedBuildWorld world;
    auto* first = world.builder(10, 12);
    auto* helper = world.builder(20, 14);
    world.build({first, helper});
    world.build({helper}, false, 30);
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* site = world.sites()[0];
    REQUIRE(helper->build_target_id() == site->entity_id());
    // Another builder completed the site, then damage arrived before this
    // helper's next tick. A queued mobile build ends; it is not a Repair.
    site->set_fraction_complete(1);
    site->set_is_being_built(false);
    site->set_health(900);
    world.tick();
    CHECK(site->health() == 900);
    CHECK(helper->build_target_id() != site->entity_id());
    REQUIRE_FALSE(helper->command_queue().empty());
    CHECK(helper->command_queue().front().target_pos.x == 30);
    CHECK(world.lua.do_string("assert(BuilderStops == 2)").ok());
}

TEST_CASE("Joining a site leaves army rejection to scripts and respects blueprint and completion",
          "[build][shared-site]") {
    SharedBuildWorld world;
    auto* first = world.builder(10, 12, 1);
    auto* helper = world.builder(20, 14);
    bool should_join = false;
    bool different_blueprint = false;
    bool completed = false;
    SECTION("allied unfinished site") {
        world.sim.set_alliance(0, 1, osc::sim::Alliance::Ally);
        should_join = true;
    }
    SECTION("enemy unfinished site without a rejecting script") {
        should_join = true;
    }
    SECTION("different blueprint") {
        world.sim.set_alliance(0, 1, osc::sim::Alliance::Ally);
        different_blueprint = true;
    }
    SECTION("finished site") {
        world.sim.set_alliance(0, 1, osc::sim::Alliance::Ally);
        completed = true;
    }
    world.build({first}, true, 20, different_blueprint ? "other_site" : "test_site");
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* original = world.sites()[0];
    if (completed) {
        original->set_is_being_built(false);
        original->set_fraction_complete(1);
    }
    world.build({helper});
    world.tick(8);
    CHECK((helper->build_target_id() == original->entity_id()) == should_join);
    CHECK(world.sites().size() == 1);
    if (!should_join) {
        CHECK(helper->command_queue().empty());
        CHECK(world.lua.do_string("assert(BuilderFailures == 1)").ok());
    }
}

TEST_CASE("An aborted shared site does not leave helpers spawning replacements",
          "[build][shared-site]") {
    SharedBuildWorld world;
    auto* first = world.builder(10, 12);
    auto* helper = world.builder(20, 14);
    world.build({first, helper});
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* site = world.sites()[0];
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, site->lua_table_ref());
    lua_setglobal(world.lua.raw(), "AbortedSite");
    const auto aborted = world.lua.do_string("moho.entity_methods.Destroy(AbortedSite)");
    UNSCOPED_INFO((aborted.ok() ? std::string() : aborted.error().message));
    REQUIRE(aborted.ok());
    world.tick(4);
    CHECK(world.sites().empty());
    CHECK(first->command_queue().empty());
    CHECK(helper->command_queue().empty());
    CHECK(world.lua.do_string("assert(SitesCreated == 1 and SitesCompleted == 0)").ok());
}

TEST_CASE("A script may reject joining another army's build without removing its frame",
          "[build][shared-site]") {
    SharedBuildWorld world;
    auto* first = world.builder(10, 12, 1);
    auto* helper = world.builder(20, 14);
    world.build({first});
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    auto* site = world.sites()[0];
    lua_rawgeti(world.lua.raw(), LUA_REGISTRYINDEX, helper->lua_table_ref());
    lua_setglobal(world.lua.raw(), "RejectingBuilder");
    REQUIRE(world.lua
                .do_string(R"(
        RejectedStarts = 0
        function RejectingBuilder:OnStartBuild(target, order)
            assert(order == 'MobileBuild')
            RejectedStarts = RejectedStarts + 1
            self:Stop()
        end
    )")
                .ok());
    world.build({helper});
    world.tick(8);
    CHECK(world.lua.do_string("assert(RejectedStarts == 1)").ok());
    CHECK(helper->command_queue().empty());
    CHECK_FALSE(helper->is_building());
    CHECK(world.sites().size() == 1);
    CHECK(site->army() == 1);
    const float before = site->fraction_complete();
    world.tick();
    CHECK(site->fraction_complete() - before == Catch::Approx(10.0f / 1000.0f * 0.1f));
    CHECK(world.sim.get_army(0)->economy().mass.requested == 0);
}

TEST_CASE("A mobile build snaps an odd footprint before preparing the shared site",
          "[build][shared-site]") {
    SharedBuildWorld world;
    REQUIRE(world.lua
                .do_string(R"(
        __blueprints.test_site.Footprint.SizeX = 1
        __blueprints.test_site.Footprint.SizeZ = 1
        __blueprints.test_site.Physics.SkirtSizeX = 1
        __blueprints.test_site.Physics.SkirtSizeZ = 1
    )")
                .ok());
    auto* builder = world.builder(10, 12);
    world.build({builder});
    world.tick(8);
    REQUIRE(world.sites().size() == 1);
    CHECK(world.sites()[0]->position().x == 20.5f);
    CHECK(world.sites()[0]->position().z == 20.5f);
    REQUIRE_FALSE(builder->command_queue().empty());
    CHECK(builder->command_queue().front().target_pos.x == 20.5f);
}
