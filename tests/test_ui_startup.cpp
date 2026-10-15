#include <catch2/catch_test_macros.hpp>

#include "core/game_state.hpp"
#include "lua/lua_state.hpp"

extern "C" {
#include <lua.h>
}

TEST_CASE("UI options callbacks run before the first menu and once per state",
          "[lua][ui][startup]") {
    // Synthetic modules: the option's callback sets a value consumed by
    // the UI bootstrap. LOUD's menu animation option uses this contract;
    // applying options after creating the menu is already too late.
    for (int generation = 0; generation != 2; ++generation) {
        osc::lua::LuaState state;
        REQUIRE(state
                    .do_string(R"(
            local applied = 0
            local optionValue
            local modules = {
                ['/lua/options/optionsLogic.lua'] = {
                    Apply = function(startup)
                        assert(startup == true)
                        applied = applied + 1
                        optionValue = 0.37
                    end,
                },
                ['/lua/ui/uimain.lua'] = {
                    SetupUI = function()
                        assert(optionValue == 0.37, 'menu option was not initialized')
                        setupCount = (setupCount or 0) + 1
                    end,
                },
            }
            function import(path) return modules[path] end
            function ApplicationCount() return applied end
        )")
                    .ok());

        // The global SetupUI route (FAF) has the same startup contract.
        if (generation == 1)
            REQUIRE(state.do_string("SetupUI = import('/lua/ui/uimain.lua').SetupUI").ok());
        lua_State* L = state.raw();
        const int top = lua_gettop(L);
        lua_pushnumber(L, 93);
        osc::core::call_setup_ui(L);
        osc::core::call_setup_ui(L);
        CHECK(lua_gettop(L) == top + 1);
        CHECK(lua_tonumber(L, top + 1) == 93);
        REQUIRE(state.do_string("assert(setupCount == 2); assert(ApplicationCount() == 1)").ok());
    }
}

TEST_CASE("A failed startup option is retried before creating UI", "[lua][ui][startup]") {
    osc::lua::LuaState state;
    REQUIRE(state
                .do_string(R"(
        attempts = 0
        setupCount = 0
        function import(path)
            if path == '/lua/options/optionsLogic.lua' then
                return { Apply = function(startup)
                    attempts = attempts + 1
                    if attempts == 1 then error('option setup failed') end
                end }
            end
            return { SetupUI = function() setupCount = setupCount + 1 end }
        end
    )")
                .ok());
    osc::core::call_setup_ui(state.raw());
    REQUIRE(state.do_string("assert(setupCount == 0)").ok());
    osc::core::call_setup_ui(state.raw());
    REQUIRE(state.do_string("assert(attempts == 2); assert(setupCount == 1)").ok());
}
