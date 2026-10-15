#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "renderer/camera.hpp"
#include "renderer/strategic_icon_renderer.hpp"
#include "renderer/texture_cache.hpp"
#include "sim/world_snapshot.hpp"

#include <array>
#include <limits>

namespace {

struct FormationIcons {
    osc::renderer::StrategicIconRenderer icons;
    osc::renderer::Camera camera;
    osc::renderer::TextureCache textures; // no GPU initialization
    osc::renderer::GPUTexture ready;
    std::array<osc::f32, 16> vp{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    FormationIcons() {
        camera.init(512, 512);
        ready.width = 32;
        ready.height = 24;
    }

    void frame(float distance = 400) {
        camera.set_eye_distance(distance);
        icons.update({}, camera, vp, nullptr, textures, 1280, 720);
    }

    void append(osc::sim::Vector3 position = {}) {
        icons.append_formation_icon(position, "/icons/invented_rest.dds", ready, vp, 1280, 720);
    }
};

} // namespace

TEST_CASE("A formation's icon marks its projected slot at strategic zoom",
          "[formation][strategic-icons]") {
    FormationIcons w;
    w.frame();
    REQUIRE(w.icons.is_strategic_zoom());
    w.append();
    w.append({0.5f, -0.5f, 0});
    REQUIRE(w.icons.quads().size() == 2);
    const auto& first = w.icons.quads()[0];
    CHECK(first.rect[0] == 624);
    CHECK(first.rect[1] == 348);
    CHECK(first.rect[2] == 32);
    CHECK(first.rect[3] == 24);
    CHECK(first.color[0] == Catch::Approx(216.0f / 255.0f));
    CHECK(first.color[1] == Catch::Approx(216.0f / 255.0f));
    CHECK(first.color[2] == 0);
    CHECK(first.color[3] == -1); // existing strategic-icon shader
    CHECK(w.icons.quads()[1].rect[0] == 944);
    CHECK(w.icons.quads()[1].rect[1] == 168);
    CHECK(w.icons.quad_textures()[0] == "/icons/invented_rest.dds");

    // The drag released: a fresh frame has no preview entries to append.
    w.frame();
    CHECK(w.icons.quads().empty());
    CHECK(w.icons.quad_textures().empty());
}

TEST_CASE("Formation icons preserve zoom, NIS and icon-scale controls",
          "[formation][strategic-icons]") {
    FormationIcons w;
    w.frame(249);
    REQUIRE_FALSE(w.icons.is_strategic_zoom());
    w.icons.set_always(true); // ordinary icons at every zoom do not duplicate mesh previews
    w.append();
    CHECK(w.icons.quads().empty());
    w.frame(251);
    w.icons.set_nis_icons(false);
    w.append();
    CHECK(w.icons.quads().empty());
    w.icons.set_nis_icons(true);
    w.icons.set_icon_scale(1.5f);
    w.append();
    REQUIRE(w.icons.quads().size() == 1);
    CHECK(w.icons.quads()[0].rect[2] == 48);
    CHECK(w.icons.quads()[0].rect[3] == 36);
    CHECK(w.icons.icon_scale() == 1.5f);
}

TEST_CASE("Formation icons reject invisible slots and respect quad capacity",
          "[formation][strategic-icons]") {
    FormationIcons w;
    w.frame();
    w.append({3, 0, 0}); // beyond the viewport
    w.append({0, std::numeric_limits<float>::quiet_NaN(), 0});
    w.vp[15] = -1; // behind the camera
    w.append();
    CHECK(w.icons.quads().empty());
    w.vp[15] = 1;
    w.ready.width = 0;
    w.append();
    CHECK(w.icons.quads().empty());
    w.ready.width = 32;
    for (unsigned i = 0; i != w.icons.MAX_ICON_QUADS + 1; ++i) w.append();
    CHECK(w.icons.quads().size() == w.icons.MAX_ICON_QUADS);
    CHECK(w.icons.quad_textures().size() == w.icons.MAX_ICON_QUADS);
    CHECK(w.icons.quad_count() == w.icons.MAX_ICON_QUADS);
}
