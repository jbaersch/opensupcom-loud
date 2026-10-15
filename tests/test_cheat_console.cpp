#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/test_status.hpp"
#include "lua/lua_state.hpp"
#include "lua/moho_bindings.hpp"
#include "lua/user_bindings.hpp"
#include "renderer/renderer.hpp"
#include "sim/build_placement.hpp"
#include "sim/sim_callback_queue.hpp"
#include "sim/sim_state.hpp"
#include "ui/console.hpp"
#include "ui/ui_control.hpp"
#include "ui/world_view.hpp"

extern "C" {
#include <lua.h>
}

#include <string>

namespace {

struct CheatConsole {
    osc::lua::LuaState lua;
    osc::sim::SimState sim{lua.raw(), nullptr};
    osc::ui::UIControlRegistry registry;
    osc::ui::Console console;
    osc::sim::SimCallbackQueue callbacks;
    osc::renderer::Renderer renderer; // no window or GPU initialization
    osc::ui::WorldView view;

    CheatConsole() {
        osc::lua::register_moho_bindings(lua, sim);
        osc::lua::register_ui_bindings(lua, registry);
        osc::lua::register_console_commands(console);
        osc::lua::register_session_console_commands(console);
        sim.add_army("ARMY_1", "Player");
        sim.add_army("ARMY_2", "AI");
        renderer.camera().init(512, 512);
        renderer.camera().set_target(200, 300);
        renderer.camera().set_zoom(150);
        renderer.set_scripted_pointer({640, 360});
        view.register_camera("WorldCamera", &renderer.camera());
        view.set_viewport(1280, 720);
        pointer("__osc_renderer", &renderer);
        pointer("__osc_world_view", &view);
        pointer("__osc_console", &console);
        pointer("__osc_sim_callback_queue", &callbacks);
        REQUIRE(lua.do_string(R"(
            PopupCalls = 0
            PopupOpen = false
            __blueprints = {test_unit = {Footprint = {SizeX = 2, SizeZ = 2}}}
            function import(path)
                assert(path == '/lua/ui/dialogs/createunit.lua')
                return {CreateDialog = function(x, y)
                    assert(type(x) == 'number' and type(y) == 'number')
                    PopupX, PopupY = x, y
                    PopupCalls = PopupCalls + 1
                    PopupOpen = not PopupOpen
                end}
            end
        )").ok());
    }

    void pointer(const char* key, void* value) {
        lua_State* L = lua.raw();
        lua_pushstring(L, key);
        lua_pushlightuserdata(L, value);
        lua_rawset(L, LUA_REGISTRYINDEX);
    }

    void cheats(bool enabled) {
        osc::sim::GameSetup setup;
        setup.options.set_string("CheatsEnabled", enabled ? "true" : "false");
        sim.set_game_setup(std::move(setup));
    }
};

} // namespace

TEST_CASE("Alt-F2's console action opens the source dialog at the pointer", "[cheat][console]") {
    CheatConsole w;
    REQUIRE(w.console.has("PopupCreateUnitMenu"));
    const int top = lua_gettop(w.lua.raw());
    REQUIRE(w.lua.do_string("ConExecute('PopupCreateUnitMenu') assert(PopupCalls == 0)").ok());
    w.cheats(true);
    REQUIRE(w.lua.do_string(R"(
        ConExecute('PopupCreateUnitMenu')
        assert(PopupCalls == 1 and PopupOpen and PopupX == 640 and PopupY == 360)
        ConExecute('PopupCreateUnitMenu')
        assert(PopupCalls == 2 and not PopupOpen)
    )").ok());
    CHECK(lua_gettop(w.lua.raw()) == top);
    CHECK(w.callbacks.empty());
    REQUIRE(w.lua.do_string("ConExecute('SetFocusArmy 1') assert(GetFocusArmy() == 2)").ok());
    w.cheats(false);
    REQUIRE(w.lua.do_string("ConExecute('SetFocusArmy 0') assert(GetFocusArmy() == 2)").ok());
    w.cheats(true);
    w.pointer("__osc_world_view", nullptr);
    REQUIRE(w.lua.do_string("ConExecute('PopupCreateUnitMenu') assert(PopupCalls == 2)").ok());
}

TEST_CASE("The create dialog's saved screen point queues a complete-unit request",
          "[cheat][console]") {
    CheatConsole w;
    REQUIRE(w.console.has("CreateUnit"));
    w.cheats(true);
    float x = 0, y = 0;
    REQUIRE(w.view.project(230, 0, 280, x, y));
    const std::string command =
        "CreateUnit test_unit 1 " + std::to_string(x) + " " + std::to_string(y);
    REQUIRE(w.lua.do_string("ConExecuteSave('" + command + "')").ok());
    auto queued = w.callbacks.drain();
    REQUIRE(queued.size() == 1);
    const auto& request = queued.front();
    CHECK(request.func_name == "__osc_CheatCreateUnit");
    CHECK(std::get<std::string>(request.args.at("Blueprint")) == "test_unit");
    CHECK(std::get<double>(request.args.at("Army")) == 1); // console army is zero-based
    CHECK(std::get<double>(request.args.at("X")) == Catch::Approx(230).margin(0.05));
    CHECK(std::get<double>(request.args.at("Y")) == Catch::Approx(0).margin(0.05));
    CHECK(std::get<double>(request.args.at("Z")) == Catch::Approx(280).margin(0.05));
    CHECK(request.unit_ids.empty());

    for (const char* bad : {"CreateUnit test_unit -1 640 360", "CreateUnit test_unit 2 640 360",
                            "CreateUnit test_unit 0.5 640 360", "CreateUnit test_unit 0 nan 360",
                            "CreateUnit test_unit 0 640 inf", "CreateUnit test_unit 0 -1 360",
                            "CreateUnit test_unit 0 2000 360", "CreateUnit test_unit 0 640",
                            "CreateUnit test_unit nonsense 640 360"}) {
        w.console.execute(w.lua.raw(), bad);
        CHECK(w.callbacks.empty());
    }
    w.cheats(false);
    w.console.execute(w.lua.raw(), command);
    CHECK(w.callbacks.empty());
    w.cheats(true);
    w.sim.set_playback(true);
    w.console.execute(w.lua.raw(), command);
    CHECK(w.callbacks.empty());
}

TEST_CASE("Source cheat dialog failures keep their diagnosis and fail test modes", "[cheat][console]") {
    struct CaptureFailures {
        bool previous = osc::test_status::count_lua_failures();
        CaptureFailures() {
            osc::test_status::reset();
            osc::test_status::set_count_lua_failures(true);
        }
        ~CaptureFailures() {
            osc::test_status::set_count_lua_failures(previous);
            osc::test_status::reset();
        }
    } capture;
    CheatConsole w;
    w.cheats(true);
    SECTION("import failed") {
        REQUIRE(w.lua.do_string("function import(path) error('source popup sentinel') end").ok());
    }
    SECTION("dialog failed") {
        REQUIRE(w.lua.do_string(R"(
            function import(path)
                return {CreateDialog = function() error('source popup sentinel') end}
            end
        )").ok());
    }
    const int top = lua_gettop(w.lua.raw());
    w.console.execute(w.lua.raw(), "PopupCreateUnitMenu");
    CHECK(lua_gettop(w.lua.raw()) == top);
    CHECK(osc::test_status::failure_count() == 1);
    const auto messages = osc::test_status::failure_messages();
    REQUIRE(messages.size() == 1);
    CHECK(messages.front().find("source popup sentinel") != std::string::npos);
}

TEST_CASE("Cheat-created footprints share the normal building grid", "[cheat][cheat-grid]") {
    CheatConsole w;
    w.cheats(true);
    REQUIRE(w.lua.do_string(R"(
        __blueprints.test_factory = {Footprint = {SizeX = 5, SizeZ = 5}}
    )").ok());
    float x = 0, y = 0;
    REQUIRE(w.view.project(211.1239f, 0, 291.7166f, x, y));
    w.console.execute(w.lua.raw(), "CreateUnit test_factory 0 " + std::to_string(x) + " " +
                                   std::to_string(y));
    auto queued = w.callbacks.drain();
    REQUIRE(queued.size() == 1);
    const auto& request = queued.front();
    const auto wx = std::get<double>(request.args.at("X"));
    const auto wz = std::get<double>(request.args.at("Z"));
    CHECK(wx == 211.5);
    CHECK(wz == 291.5);
    // An eight-cell pad on the same odd footprint can touch its neighbour
    // exactly, whether the first building was created or normally built.
    const auto factory = osc::sim::StructureSite::of(wx, wz, 5, 5, 8, 8, -1.5f, -1.5f);
    const auto neighbour = osc::sim::StructureSite::of(219.5f, 291.5f, 5, 5, 8, 8, -1.5f, -1.5f);
    CHECK(factory.x1 == neighbour.x0);
    CHECK(factory.z0 == neighbour.z0);
    CHECK(factory.z1 == neighbour.z1);
}
