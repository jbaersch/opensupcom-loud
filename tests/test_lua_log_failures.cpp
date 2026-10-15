#include <catch2/catch_test_macros.hpp>

#include "core/log.hpp"
#include "core/test_status.hpp"
#include "lua/lua_state.hpp"
#include "sim/thread_manager.hpp"

#include <spdlog/sinks/ostream_sink.h>

extern "C" {
#include <lua.h>
}

#include <memory>
#include <sstream>
#include <string>

namespace {

struct LuaDiagnostics {
    std::ostringstream output;
    std::shared_ptr<spdlog::logger> previous = spdlog::default_logger();
    bool previous_counting = osc::test_status::count_lua_failures();
    osc::lua::LuaState lua;

    LuaDiagnostics() {
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
        auto logger = std::make_shared<spdlog::logger>("lua_log_test", sink);
        logger->set_pattern("[%l] %v");
        logger->set_level(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
        osc::test_status::reset();
        osc::test_status::set_count_lua_failures(true);
    }
    ~LuaDiagnostics() {
        osc::test_status::set_count_lua_failures(previous_counting);
        osc::test_status::reset();
        spdlog::set_default_logger(previous);
    }
    LuaDiagnostics(const LuaDiagnostics&) = delete;
    LuaDiagnostics& operator=(const LuaDiagnostics&) = delete;

    void write(lua_CFunction function, const char* message) {
        output.str("");
        output.clear();
        lua_pushcfunction(lua.raw(), function);
        lua_pushstring(lua.raw(), message);
        REQUIRE(lua_pcall(lua.raw(), 1, 0, 0) == 0);
        spdlog::default_logger()->flush();
    }
};

} // namespace

TEST_CASE("Lua assertion tokens count without consuming their log messages",
          "[logs][lua-log-failure]") {
    LuaDiagnostics diagnostics;
    const char* messages[] = {
        "FAIL",
        "FAILED",
        "FAILURE",
        "[FAIL] broken check",
        "sample TEST FAILED: wrong value",
        "assertion: FAILURE; expected a value",
    };
    for (auto function : {osc::log::l_LOG, osc::log::l_WARN}) {
        for (const auto* message : messages) {
            INFO(message);
            osc::test_status::reset();
            diagnostics.write(function, message);
            CHECK(osc::test_status::failure_count() == 1);
            CHECK(osc::test_status::failure_messages() == std::vector<std::string>{message});
            CHECK(diagnostics.output.str().find(message) != std::string::npos);
            const char* level = function == osc::log::l_LOG ? "[info]" : "[warning]";
            CHECK(diagnostics.output.str().find(level) != std::string::npos);
        }
    }
}

TEST_CASE("Lua diagnostic words are visible without becoming assertion failures",
          "[logs][lua-log-failure]") {
    LuaDiagnostics diagnostics;
    const char* messages[] = {
        "operation FAILS and will retry",
        "FAILSAFE remains enabled",
        "PREFAILURE state",
        "FAIL_COUNTER = 0",
        "object7FAILED2 is an identifier",
        "lowercase failure is a word",
    };
    for (auto function : {osc::log::l_LOG, osc::log::l_WARN}) {
        for (const auto* message : messages) {
            INFO(message);
            osc::test_status::reset();
            diagnostics.write(function, message);
            CHECK(osc::test_status::failure_count() == 0);
            CHECK(osc::test_status::failure_messages().empty());
            CHECK(diagnostics.output.str().find(message) != std::string::npos);
            const char* level = function == osc::log::l_LOG ? "[info]" : "[warning]";
            CHECK(diagnostics.output.str().find(level) != std::string::npos);
        }
    }
    osc::test_status::set_count_lua_failures(false);
    diagnostics.write(osc::log::l_WARN, "FAIL: a normal play diagnostic");
    CHECK(osc::test_status::failure_count() == 0);
    CHECK(diagnostics.output.str().find("FAIL: a normal play diagnostic") != std::string::npos);
}

TEST_CASE("Lua thread exceptions count independently of assertion log tokens",
          "[threads][logs][lua-log-failure]") {
    LuaDiagnostics diagnostics;
    osc::sim::ThreadManager threads(diagnostics.lua.raw());
    REQUIRE(diagnostics.lua
                .do_string(R"(
        function broken_thread() error('synthetic runtime problem') end
    )")
                .ok());
    lua_settop(diagnostics.lua.raw(), 0);
    lua_getglobal(diagnostics.lua.raw(), "broken_thread");
    threads.fork_thread(diagnostics.lua.raw());
    lua_settop(diagnostics.lua.raw(), 0);
    threads.resume_all(1);
    CHECK(threads.active_count() == 0);
    REQUIRE(osc::test_status::failure_count() == 1);
    const auto messages = osc::test_status::failure_messages();
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].find("Lua thread error:") != std::string::npos);
    CHECK(messages[0].find("synthetic runtime problem") != std::string::npos);
    CHECK(diagnostics.output.str().find("synthetic runtime problem") != std::string::npos);
}
