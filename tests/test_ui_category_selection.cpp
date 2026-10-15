#include <catch2/catch_test_macros.hpp>

#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/user_bindings.hpp"
#include "renderer/input_handler.hpp"
#include "sim/manipulator.hpp"
#include "sim/shield.hpp"
#include "sim/sim_state.hpp"
#include "sim/unit.hpp"

extern "C" {
#include <lua.h>
}

#include <memory>
#include <string>
#include <unordered_set>

namespace {

struct SelectionWorld {
    osc::lua::LuaState sim_lua;
    osc::sim::SimState sim{sim_lua.raw(), nullptr};
    osc::lua::LuaState ui;
    osc::renderer::InputHandler input;

    SelectionWorld() {
        osc::lua::register_moho_bindings(ui, sim);
        osc::lua::register_user_bindings(ui);
        input.set_player_army(2);
        lua_pushstring(ui.raw(), "__osc_input_handler");
        lua_pushlightuserdata(ui.raw(), &input);
        lua_rawset(ui.raw(), LUA_REGISTRYINDEX);
    }

    osc::sim::Unit& add(int army, const char* category, bool selectable = true) {
        auto unit = std::make_unique<osc::sim::Unit>();
        unit->set_army(army);
        unit->add_category(category);
        if (selectable) unit->add_category("SELECTABLE");
        const auto id = sim.entity_registry().register_entity(std::move(unit));
        return *static_cast<osc::sim::Unit*>(sim.entity_registry().find(id));
    }
};

} // namespace

TEST_CASE("UI category enumeration supports saving and restoring the player's selection",
          "[userunit][selection][ui-category]") {
    SelectionWorld w;
    const auto commander = w.add(2, "COMMAND").entity_id();
    const auto engineer = w.add(2, "ENGINEER").entity_id();
    w.add(0, "ENGINEER");
    w.add(2, "ENGINEER", false);
    w.add(2, "ENGINEER").mark_destroyed();
    w.input.set_selected({commander});

    // A UI mod may briefly enumerate its army, then restore the player's
    // selection synchronously in the same beat. The returned units must
    // be real UserUnits, without exposing enemy or dead entities.
    const auto result = w.ui.do_string(R"(
        local previous = GetSelectedUnits()
        UISelectionByCategory('ALLUNITS', false, false, false, false)
        scanned = {}
        scanCount = 0
        for _, unit in GetSelectedUnits() do
            scanned[unit:GetEntityId()] = true
            scanCount = scanCount + 1
        end
        SelectUnits(previous)
    )");
    INFO((result.ok() ? "" : result.error().message));
    REQUIRE(result.ok());
    REQUIRE(w.ui.do_string("assert(scanCount == 2); assert(scanned['" + std::to_string(commander) +
                           "']); assert(scanned['" + std::to_string(engineer) + "'])")
                .ok());
    CHECK(w.input.selected() == std::unordered_set<osc::u32>{commander});

    w.input.set_selected({});
    REQUIRE(w.ui.do_string(R"(
        local previous = GetSelectedUnits()
        UISelectionByCategory('ALLUNITS', false, false, false, false)
        SelectUnits(previous)
    )")
                .ok());
    CHECK(w.input.selected().empty());
}

TEST_CASE("UI category selection uses the category, append and idle arguments",
          "[userunit][selection][ui-category]") {
    SelectionWorld w;
    const auto commander = w.add(2, "COMMAND").entity_id();
    const auto engineer = w.add(2, "ENGINEER").entity_id();
    w.add(2, "ENGINEER").set_busy(true);
    w.add(0, "ENGINEER");
    w.input.set_selected({commander});

    REQUIRE(w.ui.do_string("UISelectionByCategory('ENGINEER', false, false, false, true)").ok());
    CHECK(w.input.selected() == std::unordered_set<osc::u32>{engineer});
    REQUIRE(w.ui.do_string("UISelectionByCategory('COMMAND', true, false, false, false)").ok());
    CHECK(w.input.selected() == std::unordered_set<osc::u32>{engineer, commander});
    REQUIRE(w.ui.do_string("UISelectionByCategory('NAVAL', false, false, false, false)").ok());
    CHECK(w.input.selected().empty());
}
