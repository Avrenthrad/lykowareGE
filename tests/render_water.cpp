// render.water (ADR 0097): water surfaces. A pool over sand is seen from above and from near the
// horizon; each pixel must match the stated blend within 3 of 255: the lit body colour mixed
// towards the reflection by F = 0.02 + 0.98 (1 - N.V)^5, over what is behind it with alpha
// opacity mixed towards 1 by F. Without the shaders the component changes nothing.
//
// Usage: lykoware_render_water_test LIT.vert LIT.frag WATER.vert WATER.frag SHADOW.vert
//        LIT_SHADOWED.vert LIT_SHADOWED.frag POST.vert POST.frag SKINNED.vert
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device). A software
// GPU without Vulkan skips; it never passes.

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace render = lykoware::render;
namespace rhi = lykoware::rhi;
namespace ecs = lykoware::ecs;

constexpr int exit_skip = 77;
constexpr std::uint32_t width = 128;
constexpr std::uint32_t height = 96;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

const rhi::ClearColor sky{0.F, 0.F, 0.F, 1.F};
// Looking almost straight down at the pool, and almost along it.
const render::Camera above{{0.0, 10.0, 1.0}, {0.0, 0.0, 0.0}};
const render::Camera along{{0.0, 1.0, 10.0}, {0.0, 0.0, 0.0}};

const math::Vec3 sand{0.6F, 0.5F, 0.3F};
const math::Vec3 body{0.1F, 0.3F, 0.4F};
const render::Water water{{0.45F, 0.6F, 0.8F}};

using Rgb = std::array<int, 3>;

Rgb at(const lykoware::Image& image, const render::Camera& camera, math::DVec3 point)
{
    const math::Mat4 view = math::look_at({}, math::relative_to(camera.target, camera.eye), camera.up);
    const math::Mat4 projection =
        math::perspective(camera.vertical_fov, static_cast<float>(width) / static_cast<float>(height), camera.near);
    const math::Vec3 p = math::relative_to(point, camera.eye);
    const math::Vec4 clip = projection * view * math::Vec4{p.x, p.y, p.z, 1.F};
    const auto x = static_cast<std::size_t>((clip.x / clip.w * 0.5F + 0.5F) * static_cast<float>(width));
    const auto y = static_cast<std::size_t>((clip.y / clip.w * 0.5F + 0.5F) * static_cast<float>(height));
    const std::size_t i = (y * image.width() + x) * 3;
    if (i + 2 >= image.pixels().size()) {
        return {-1, -1, -1};
    }
    return {image.pixels()[i], image.pixels()[i + 1], image.pixels()[i + 2]};
}

std::string text(const Rgb& c)
{
    return "(" + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", " + std::to_string(c[2]) + ")";
}

int furthest(const Rgb& a, const Rgb& b)
{
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}

// What the pool's top should show at the origin: ADR 0097's formula over `behind`.
Rgb expected(const render::Camera& camera, float opacity, const Rgb& behind)
{
    const render::Lighting lighting;
    const float diffuse = std::max(0.F, math::normalize(lighting.direction).y);
    const float shade = lighting.ambient + (1.F - lighting.ambient) * diffuse;
    const math::Vec3 to_eye = math::normalize(math::relative_to(camera.eye, math::DVec3{}));
    const float away = 1.F - std::max(0.F, to_eye.y);
    const float mirror = 0.02F + 0.98F * away * away * away * away * away;
    const float alpha = opacity + (1.F - opacity) * mirror;
    const float lit[3] = {body.x * shade, body.y * shade, body.z * shade};
    const float mirrored[3] = {water.reflection.x, water.reflection.y, water.reflection.z};
    Rgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float colour = lit[i] + (mirrored[i] - lit[i]) * mirror;
        out[i] = static_cast<int>(std::lround(colour * alpha * 255.F + static_cast<float>(behind[i]) * (1.F - alpha)));
    }
    return out;
}

struct Scene {
    bool pool = true;
    float opacity = 0.5F;
    std::optional<render::Water> surface = water;
    bool sunk_cube = false;
};

// Sand whose top is 1 m down, and a pool 10 m square whose top is at y = 0.
ecs::World build(const Scene& scene)
{
    ecs::World entities;
    const ecs::Entity ground = entities.create();
    entities.add<lykoware::Transform>(ground, lykoware::Transform{{0.0, -1.5, 0.0}, {}});
    entities.add<render::MeshRenderer>(ground, render::MeshRenderer{render::MeshShape::Cube, {80.F, 1.F, 80.F}, sand});
    if (scene.sunk_cube) {
        const ecs::Entity cube = entities.create();
        entities.add<lykoware::Transform>(cube, lykoware::Transform{{0.0, -0.6, 0.0}, {}});
        entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {0.8F, 0.8F, 0.8F}, {1.F, 0.F, 0.F}});
    }
    if (scene.pool) {
        const ecs::Entity pool = entities.create();
        entities.add<lykoware::Transform>(pool, lykoware::Transform{{0.0, -0.1, 0.0}, {}});
        render::MeshRenderer mesh{render::MeshShape::Cube, {10.F, 0.2F, 10.F}, body};
        mesh.opacity = scene.opacity;
        mesh.water = scene.surface;
        entities.add<render::MeshRenderer>(pool, mesh);
    }
    return entities;
}

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const Scene& scene, const render::Camera& camera,
                     std::string_view name)
{
    rhi::RhiError error;
    lykoware::Image image;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const bool ok = target && renderer.draw(build(scene), camera, *target, sky, error) && target->read_back(image, error);
    expect(ok && image.width() == width, std::string(name) + ": renders (" + error.message + ")");
    return image;
}

bool same(const lykoware::Image& a, const lykoware::Image& b)
{
    return a.pixels().size() == b.pixels().size() && std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin());
}

bool refused(rhi::Device& device, render::Renderer& renderer, const Scene& scene)
{
    rhi::RhiError error;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    return target && !renderer.draw(build(scene), above, *target, sky, error) && error.failure == rhi::RhiFailure::InvalidArgument;
}

std::vector<std::uint32_t> words(const char* path)
{
    std::vector<std::uint32_t> out;
    rhi::RhiError error;
    if (!rhi::read_spirv_file(path, out, error)) {
        std::cerr << "FAIL: " << path << " loads: " << error.message << '\n';
        ++g_failures;
    }
    return out;
}

}

int main(int argc, char* argv[])
{
    if (argc != 11) {
        std::cerr << "usage: lykoware_render_water_test LIT.vert LIT.frag WATER.vert WATER.frag SHADOW.vert"
                     " LIT_SHADOWED.vert LIT_SHADOWED.frag POST.vert POST.frag SKINNED.vert\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.water test";
    instance_config.validation = rhi::Validation::IfAvailable;
    rhi::RhiError error;
    auto instance = rhi::Instance::create(instance_config, error);
    if (!instance) {
        const bool unavailable = error.failure == rhi::RhiFailure::NoLoader || error.failure == rhi::RhiFailure::NoDriver
            || error.failure == rhi::RhiFailure::VersionTooOld;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }
    auto device = rhi::Device::create(*instance, {}, error);
    if (!device) {
        const bool unavailable = error.failure == rhi::RhiFailure::NoSuitableDevice;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }
    std::vector<std::vector<std::uint32_t>> spirv;
    for (int i = 1; i < argc; ++i) {
        spirv.push_back(words(argv[i]));
    }
    if (g_failures > 0) {
        return EXIT_FAILURE;
    }
    const render::WaterShaders water_shaders{spirv[2], spirv[3]};
    {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, spirv[0], spirv[1], error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        const math::DVec3 centre{};
        Scene plain;
        plain.surface.reset();

        // Without water shaders the component is ignored: the pool is an ordinary opaque box.
        const lykoware::Image ignored = draw(*device, *renderer, Scene{}, above, "water without shaders");
        const lykoware::Image opaque = draw(*device, *renderer, plain, above, "the pool without the component");
        expect(!renderer->water_enabled() && same(ignored, opaque) && renderer->last_water_draw_count() == 0,
               "without water shaders MeshRenderer::water changes nothing");
        expect(!renderer->enable_water({}, error) && !renderer->water_enabled(), "missing water shaders are refused");

        expect(renderer->enable_water(water_shaders, error) && renderer->water_enabled(), "water enables: " + error.message);
        Scene dry;
        dry.pool = false;
        for (const render::Camera* camera : {&above, &along}) {
            const std::string view = camera == &above ? "from above" : "along the surface";
            const Rgb behind = at(draw(*device, *renderer, dry, *camera, "the sand alone"), *camera, centre);
            const Rgb seen = at(draw(*device, *renderer, Scene{}, *camera, "the pool"), *camera, centre);
            const Rgb want = expected(*camera, 0.5F, behind);
            expect(renderer->last_water_draw_count() == 1 && renderer->last_transparent_draw_count() == 0,
                   view + ": the pool is one water draw and no transparent one");
            expect(furthest(seen, want) <= 3, view + ": the pool matches the formula: " + text(seen) + " vs " + text(want));
            // Deliberate break: with the pool's draw removed the pixel is bare sand, far from the formula.
            expect(furthest(behind, want) > 20, view + ": without the pool the sand shows instead: " + text(behind));
        }
        // The mirror grows towards the horizon: more of the reflection's blue, less of the sand's red.
        const Rgb steep = at(draw(*device, *renderer, Scene{}, above, "steep"), above, centre);
        const Rgb glancing = at(draw(*device, *renderer, Scene{}, along, "glancing"), along, centre);
        expect(glancing[2] > steep[2] + 30, "the surface mirrors more at a glancing angle: " + text(glancing) + " vs " + text(steep));

        // Head-on opacity 1 hides the sand; a cube under clearer water shows through.
        Scene solid;
        solid.opacity = 1.F;
        const Rgb full = at(draw(*device, *renderer, solid, above, "opacity 1"), above, centre);
        expect(furthest(full, expected(above, 1.F, {0, 0, 0})) <= 3, "at opacity 1 nothing behind shows: " + text(full));
        Scene sunk;
        sunk.sunk_cube = true;
        const Rgb over_cube = at(draw(*device, *renderer, sunk, above, "a cube under the pool"), above, centre);
        expect(over_cube[0] > steep[0] + 30, "a red cube under the water shows through it: " + text(over_cube));

        // Bad values are refused while water is on.
        Scene bad;
        bad.opacity = 1.5F;
        expect(refused(*device, *renderer, bad), "a water opacity above 1 is refused");
        bad = Scene{};
        bad.surface = render::Water{{0.5F, std::numeric_limits<float>::quiet_NaN(), 0.5F}};
        expect(refused(*device, *renderer, bad), "a NaN reflection is refused");
        bad.surface = render::Water{{0.5F, 0.5F, -0.1F}};
        expect(refused(*device, *renderer, bad), "a negative reflection is refused");

        // Water casts no shadow: with shadows on, only the sand is a caster.
        if (renderer->enable_shadows({spirv[4], spirv[5], spirv[6]}, {}, error)) {
            static_cast<void>(draw(*device, *renderer, Scene{}, above, "the pool with shadows"));
            expect(renderer->last_shadow_caster_count() == 1 && renderer->last_water_draw_count() == 1,
                   "the pool casts no shadow (" + std::to_string(renderer->last_shadow_caster_count()) + " casters)");
            renderer->disable_shadows();
        } else {
            expect(false, "shadows enable: " + error.message);
        }

        // Through the HDR target and tone map the pool is still drawn.
        if (renderer->enable_post({spirv[7], spirv[8]}, {}, error)) {
            const Rgb mapped_sand = at(draw(*device, *renderer, dry, along, "sand with post"), along, centre);
            const Rgb mapped_pool = at(draw(*device, *renderer, Scene{}, along, "the pool with post"), along, centre);
            expect(renderer->last_water_draw_count() == 1 && furthest(mapped_pool, mapped_sand) > 20,
                   "with post-processing on the pool is still drawn: " + text(mapped_pool) + " vs " + text(mapped_sand));
            renderer->disable_post();
        } else {
            expect(false, "post enables: " + error.message);
        }

        renderer->disable_water();
        const lykoware::Image off = draw(*device, *renderer, Scene{}, above, "water disabled");
        expect(!renderer->water_enabled() && same(off, opaque) && renderer->last_water_draw_count() == 0,
               "disabling water draws exactly as before it was enabled");
    }
    {
        auto skinned = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, spirv[0], spirv[1], error, spirv[9]);
        expect(skinned && !skinned->enable_water(water_shaders, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "a skinned renderer refuses water");
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.water: the pool matches the formula from above and along the surface\n";
    return EXIT_SUCCESS;
}
