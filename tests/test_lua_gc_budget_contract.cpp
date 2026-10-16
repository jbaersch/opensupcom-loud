#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FinalizerContext {
    unsigned tick = 0;
    int metatable_ref = -2;
    std::vector<std::pair<unsigned, int>> events;
};

int finalize(lua_State* lua) {
    auto& context = *static_cast<FinalizerContext*>(lua_touserdata(lua, lua_upvalueindex(1)));
    const int id = *static_cast<int*>(lua_touserdata(lua, 1));
    context.events.emplace_back(context.tick, id);
    if (id == 1) {
        lua_pushstring(lua, "resurrected");
        lua_pushvalue(lua, 1);
        lua_rawset(lua, LUA_GLOBALSINDEX);
    }
    return 0;
}

int make_userdata(lua_State* lua) {
    auto& context = *static_cast<FinalizerContext*>(lua_touserdata(lua, lua_upvalueindex(1)));
    const int id = static_cast<int>(luaL_checknumber(lua, 1));
    *static_cast<int*>(lua_newuserdata(lua, sizeof(int))) = id;
    lua_rawgeti(lua, LUA_REGISTRYINDEX, context.metatable_ref);
    lua_setmetatable(lua, -2);
    return 1;
}

struct Scenario {
    FinalizerContext context;
    lua_State* lua = lua_open();

    Scenario() {
        REQUIRE(lua);
        context.events.reserve(3);
        lua_setmanualgc(lua, 1);
        lua_setlazysweep(lua, 1);
        luaopen_base(lua);
        luaopen_table(lua);
        luaopen_string(lua);
        lua_settop(lua, 0);
        lua_newtable(lua);
        lua_pushstring(lua, "__gc");
        lua_pushlightuserdata(lua, &context);
        lua_pushcclosure(lua, finalize, 1);
        lua_rawset(lua, -3);
        context.metatable_ref = luaL_ref(lua, LUA_REGISTRYINDEX);
        lua_pushstring(lua, "MakeUserdata");
        lua_pushlightuserdata(lua, &context);
        lua_pushcclosure(lua, make_userdata, 1);
        lua_rawset(lua, LUA_GLOBALSINDEX);
    }
    ~Scenario() {
        if (lua) lua_close(lua);
    }
    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    void run(const std::string& code, int results = 0) const {
        int status = luaL_loadbuffer(lua, code.data(), code.size(), "GC budget contract");
        if (status == 0) status = lua_pcall(lua, 0, results, 0);
        if (status != 0) INFO("Lua error: " << lua_tostring(lua, -1));
        REQUIRE(status == 0);
    }
    std::string observe() const {
        run("return snapshot()", 1);
        REQUIRE(lua_type(lua, -1) == LUA_TSTRING);
        const std::string result = lua_tostring(lua, -1);
        lua_pop(lua, 1);
        return result;
    }
    void close() {
        lua_close(lua);
        lua = nullptr;
    }
};

struct Outcome {
    std::vector<std::pair<unsigned, int>> finalizers;
    std::vector<std::string> observations;
};

Outcome run_budget(int budget) {
    INFO("sweep budget " << budget);
    Scenario scene;
    scene.run(R"lua(
        weak_k = setmetatable({}, {__mode='k'})
        weak_v = setmetatable({}, {__mode='v'})
        weak_kv = setmetatable({}, {__mode='kv'})
        weak_userdata = setmetatable({}, {__mode='v'})
        held_key, held_value = {}, {}
        weak_k[{}], weak_k[held_key] = 'dead', 'held'
        weak_v.dead, weak_v.held = {}, held_value
        weak_kv[{}], weak_kv[held_key] = {}, held_value
        weak_userdata.first = MakeUserdata(1)
        weak_userdata.second = MakeUserdata(2)
        held_userdata = MakeUserdata(3)
        weak_userdata.third = held_userdata
        -- Enough unreachable tables that even the larger budget still has
        -- an unfinished sweep when fresh objects are allocated at tick 71.
        for i=1,60000 do local garbage = {i, sub={i}} end
        local function count(t)
            local n = 0
            for k,v in pairs(t) do n=n+1 end
            return n
        end
        function snapshot()
            return count(weak_k)..','..count(weak_v)..','..count(weak_kv)..','..
                   count(weak_userdata)..','..tostring(resurrected~=nil)..','..
                   (fresh and fresh[100].value or 0)
        end
    )lua");
    Outcome result;
    result.observations.reserve(5);
    for (unsigned tick = 1; tick <= 140; ++tick) {
        scene.context.tick = tick;
        lua_sweepstep(scene.lua, budget);
        if (tick == 71) {
            REQUIRE(lua_sweepstep(scene.lua, 0) == 0);
            scene.run(R"lua(
                fresh = {}
                for i=1,100 do fresh[i] = {value=i} end
                weak_v.fresh = fresh
            )lua");
        }
        if (tick == 100) scene.run("held_userdata=nil");
        if (tick == 70 || tick == 140) lua_setgcthreshold(scene.lua, 0);
        if (tick == 69 || tick == 70 || tick == 71 || tick == 139 || tick == 140)
            result.observations.push_back(scene.observe());
        if (tick < 70) CHECK(scene.context.events.empty());
        if (tick > 70 && tick < 140) CHECK(scene.context.events.size() == 2);
    }
    const std::vector<std::pair<unsigned, int>> expected{{70, 2}, {70, 1}, {140, 3}};
    CHECK(scene.context.events == expected);
    // Clearing the resurrected reference and closing must not finalize it
    // a second time; Lua marks userdata finalized before calling __gc.
    scene.run("resurrected=nil");
    scene.context.tick = 141;
    scene.close();
    CHECK(scene.context.events == expected);
    result.finalizers = scene.context.events;
    return result;
}

} // namespace

TEST_CASE("Sweep budgets preserve full-GC weak tables, finalizer order and resurrection ticks",
          "[lua_gc][gc-budget-contract]") {
    const Outcome baseline = run_budget(37);
    const std::vector<std::string> expected{"2,2,2,3,false,0", "1,1,1,1,true,0", "1,2,1,1,true,100",
                                            "1,2,1,1,true,100", "1,2,1,0,true,100"};
    REQUIRE(baseline.observations == expected);
    for (const int budget : std::array{80'000, 320'000}) {
        const Outcome candidate = run_budget(budget);
        CHECK(candidate.observations == baseline.observations);
        CHECK(candidate.finalizers == baseline.finalizers);
    }
}
