#include <catch2/catch_test_macros.hpp>

#include "sim/category_expr.hpp"
#include "sim/category_set.hpp"
#include "sim/manipulator.hpp"
#include "sim/state_io.hpp"
#include "sim/unit.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <array>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace osc::sim;
using Cats = std::unordered_set<std::string>;

CategoryBits bits_for(const Cats& categories) {
    CategoryBits bits;
    for (const auto& name : categories) bits.set(CategoryIds::intern(name));
    return bits;
}

void check_equivalent(const CategoryExpr& expression, const Cats& categories) {
    CHECK(expression.matches_bits(bits_for(categories)) == expression.matches(categories));
}

// The existing byte contract deliberately has no process-local category IDs.
void write_reference(StateWriter& writer, const CategoryExpr& expression) {
    writer.u8v(static_cast<osc::u8>(expression.op()));
    writer.str(expression.category_name());
    writer.size(expression.operands().size());
    for (const auto& child : expression.operands()) write_reference(writer, child);
}

} // namespace

TEST_CASE("Compiled category bits match string expressions for every category subset",
          "[category][category-bit-contract]") {
    const std::array<std::string, 9> names{
        "AIR", "LAND", "MOBILE", "TECH3", "NAVAL", "ALLPROJECTILES", "air", "", "UNKNOWN_MOD"};
    const std::vector<CategoryExpr> expressions{
        {},
        CategoryExpr::all(),
        CategoryExpr::name("AIR"),
        CategoryExpr::name("air"),
        CategoryExpr::name(""),
        CategoryExpr::name("UNKNOWN_MOD"),
        CategoryExpr::name("NEVER_PRESENT"),
        CategoryExpr::name("ALLPROJECTILES"),
        parse_category_list("AIR MOBILE, NAVAL TECH3"),
        CategoryExpr::combine(CategoryExpr::Op::Difference, CategoryExpr::all(),
                              CategoryExpr::name("AIR")),
        CategoryExpr::combine(
            CategoryExpr::Op::Intersection,
            CategoryExpr::combine(CategoryExpr::Op::Union, CategoryExpr::name("TECH3"),
                                  CategoryExpr::name("NAVAL")),
            CategoryExpr::combine(CategoryExpr::Op::Difference, CategoryExpr::all(),
                                  CategoryExpr::name("LAND"))),
        CategoryExpr::combine(CategoryExpr::Op::Union, {}, CategoryExpr::name("MOBILE")),
        // combine's public Op argument retains its existing behavior even
        // when the caller supplies a leaf operator rather than a set operator.
        CategoryExpr::combine(CategoryExpr::Op::All, {}, {}),
        CategoryExpr::combine(CategoryExpr::Op::Name, {}, {})};
    for (unsigned subset = 0; subset < (1u << names.size()); ++subset) {
        INFO("subset " << subset);
        Cats categories;
        for (unsigned i = 0; i < names.size(); ++i)
            if (subset & (1u << i)) categories.insert(names[i]);
        for (const auto& expression : expressions) check_equivalent(expression, categories);
    }
}

TEST_CASE("Category expressions keep matching categories added after compilation and copies",
          "[category][category-bit-contract]") {
    const std::string future = "__OSC_CATEGORY_ADDED_AFTER_WEAPON_COMPILATION";
    CategoryExpr expression = CategoryExpr::name(future);
    CategoryExpr copied = expression;
    CategoryExpr moved = std::move(copied);
    Unit unit;
    CHECK_FALSE(expression.matches_bits(unit.category_bits()));
    unit.add_category(future);
    CHECK(expression.matches_bits(unit.category_bits()));
    CHECK(moved.matches_bits(unit.category_bits()));
    CHECK(moved.matches(unit.categories()));
    copied = CategoryExpr::all();
    CHECK(copied.matches_bits(unit.category_bits()));
    // ALLUNITS follows the existing exclusion, even on an unusual Unit
    // carrying ALLPROJECTILES; it does not mean "C++ object is a Unit".
    unit.add_category("ALLPROJECTILES");
    CHECK_FALSE(copied.matches_bits(unit.category_bits()));
    CHECK_FALSE(copied.matches(unit.categories()));
    copied = expression;
    CHECK(copied.matches_bits(unit.category_bits()));
}

TEST_CASE("Lua category expressions retain owned bit tests after the Lua state closes",
          "[category][category-bit-contract]") {
    lua_State* lua = lua_open();
    REQUIRE(lua);
    constexpr char chunk[] = "return {__op='difference',__left={__name='AIR'},"
                             "__right={__op='union',__left={__name='HIGHALTAIR'},"
                             "__right={__name='UNTARGETABLE'}}}";
    REQUIRE(luaL_loadbuffer(lua, chunk, sizeof(chunk) - 1, "category bits fixture") == 0);
    REQUIRE(lua_pcall(lua, 0, 1, 0) == 0);
    CategoryExpr expression = compile_category(lua, -1);
    lua_close(lua);
    check_equivalent(expression, {"AIR"});
    check_equivalent(expression, {"AIR", "HIGHALTAIR"});
    check_equivalent(expression, {"AIR", "UNTARGETABLE"});
    check_equivalent(expression, {"LAND"});
}

TEST_CASE("Category snapshots rebuild derived IDs and keep the previous serialized bytes",
          "[category][category-bit-contract][save]") {
    CategoryExpr original;
    SECTION("Empty") {}
    SECTION("All units") {
        original = CategoryExpr::all();
    }
    SECTION("Name") {
        original = CategoryExpr::name("__OSC_CATEGORY_RESTORED_NAME");
    }
    SECTION("Compound") {
        original = CategoryExpr::combine(CategoryExpr::Op::Difference,
                                         parse_category_list("AIR TECH3, NAVAL"),
                                         CategoryExpr::name("UNTARGETABLE"));
    }
    std::vector<osc::u8> expected, saved, restored;
    StateWriter reference{expected};
    write_reference(reference, original);
    StateWriter writer{saved};
    StateIO::save(writer, original);
    REQUIRE(saved == expected);
    // Load over a different precompiled expression: a stale cache must not
    // survive this, or the All case would test the wrong exclusion bit.
    CategoryExpr loaded = CategoryExpr::name("__OSC_WRONG_OLD_CATEGORY_ID");
    StateReader reader{saved};
    StateIO::load(reader, loaded);
    REQUIRE(reader.ok());
    REQUIRE(reader.at_end());
    for (const Cats& categories :
         {Cats{}, Cats{"__OSC_WRONG_OLD_CATEGORY_ID"}, Cats{"__OSC_CATEGORY_RESTORED_NAME"},
          Cats{"AIR", "TECH3"}, Cats{"NAVAL"}, Cats{"NAVAL", "UNTARGETABLE"},
          Cats{"ALLPROJECTILES"}}) {
        CHECK(loaded.matches_bits(bits_for(categories)) == original.matches(categories));
        CHECK(loaded.matches(categories) == original.matches(categories));
    }
    StateWriter second{restored};
    StateIO::save(second, loaded);
    CHECK(restored == saved);
}
