// render.sky (ADR 0098): the sky behind the scene. With no entities every pixel must match
// mix(horizon, zenith, max(d.y, 0)) for its own view direction d within 2 of 255; looking at the
// light, the centre shows the sun. The scene draws over the sky unchanged, and with the sky off
// the clear colour is back, byte for byte.
//
// Usage: lykoware_render_sky_test LIT.vert LIT.frag SKY.vert SKY.frag POST.vert POST.frag SKINNED.vert
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

using Rgb = std::array<int, 3>;

const rhi::ClearColor clear{0.F, 0.F, 0.F, 1.F};
const render::Camera level{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}};

Rgb pixel(const lykoware::Image& image, std::uint32_t x, std::uint32_t y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * image.width() + x) * 3;
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

// The unit view direction through the centre of pixel (x, y), in world space.
math::Vec3 direction(const render::Camera& camera, std::uint32_t x, std::uint32_t y)
{
    const math::Vec3 forward = math::normalize(math::relative_to(camera.target, camera.eye));
    const math::Vec3 right = math::normalize(math::cross(forward, camera.up));
    const math::Vec3 up = math::cross(right, forward);
    const float tan_y = std::tan(camera.vertical_fov * 0.5F);
    const float tan_x = tan_y * static_cast<float>(width) / static_cast<float>(height);
    const float ndc_x = (static_cast<float>(x) + 0.5F) / static_cast<float>(width) * 2.F - 1.F;
    const float ndc_y = (static_cast<float>(y) + 0.5F) / static_cast<float>(height) * 2.F - 1.F;
    return math::normalize(forward + right * (ndc_x * tan_x) + up * (-ndc_y * tan_y));
}

math::Vec3 gradient(const render::Sky& sky, math::Vec3 d)
{
    const float t = std::max(d.y, 0.F);
    return sky.horizon + (sky.zenith - sky.horizon) * t;
}

Rgb bytes(math::Vec3 colour)
{
    return {static_cast<int>(std::lround(std::min(colour.x, 1.F) * 255.F)), static_cast<int>(std::lround(std::min(colour.y, 1.F) * 255.F)),
            static_cast<int>(std::lround(std::min(colour.z, 1.F) * 255.F))};
}

math::Vec3 reinhard(math::Vec3 c)
{
    return {c.x / (1.F + c.x), c.y / (1.F + c.y), c.z / (1.F + c.z)};
}

ecs::World with_cube()
{
    ecs::World entities;
    const ecs::Entity cube = entities.create();
    entities.add<lykoware::Transform>(cube, lykoware::Transform{{0.0, 0.0, -4.0}, {}});
    entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.9F, 0.2F, 0.2F}});
    return entities;
}

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const ecs::World& entities, const render::Camera& camera,
                     std::string_view name)
{
    rhi::RhiError error;
    lykoware::Image image;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const bool ok = target && renderer.draw(entities, camera, *target, clear, error) && target->read_back(image, error);
    expect(ok && image.width() == width, std::string(name) + ": renders (" + error.message + ")");
    return image;
}

bool same(const lykoware::Image& a, const lykoware::Image& b)
{
    return a.pixels().size() == b.pixels().size() && std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin());
}

// Every 8th pixel of the image against the gradient; the worst channel difference.
int worst_gradient(const lykoware::Image& image, const render::Camera& camera, const render::Sky& sky, bool tone_mapped)
{
    int worst = 0;
    for (std::uint32_t y = 4; y < height; y += 8) {
        for (std::uint32_t x = 4; x < width; x += 8) {
            const math::Vec3 colour = gradient(sky, direction(camera, x, y));
            worst = std::max(worst, furthest(pixel(image, x, y), bytes(tone_mapped ? reinhard(colour) : colour)));
        }
    }
    return worst;
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
    if (argc != 8) {
        std::cerr << "usage: lykoware_render_sky_test LIT.vert LIT.frag SKY.vert SKY.frag POST.vert POST.frag SKINNED.vert\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.sky test";
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
    const render::SkyShaders sky_shaders{spirv[2], spirv[3]};
    {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, spirv[0], spirv[1], error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        const ecs::World nothing;
        const ecs::World cube = with_cube();
        const std::uint32_t cx = width / 2;
        const std::uint32_t cy = height / 2;
        const lykoware::Image blank = draw(*device, *renderer, nothing, level, "no sky");
        const lykoware::Image cube_alone = draw(*device, *renderer, cube, level, "the cube without a sky");
        expect(!renderer->sky() && pixel(blank, cx, 4) == Rgb{0, 0, 0}, "without a sky the clear colour shows");

        const render::Sky sky;
        expect(!renderer->enable_sky({}, sky, error) && !renderer->sky(), "missing sky shaders are refused");
        expect(renderer->enable_sky(sky_shaders, sky, error) && renderer->sky().has_value(), "the sky enables: " + error.message);

        // Level view: the top of the image looks up 30 degrees, the bottom half is below the horizon.
        const lykoware::Image open = draw(*device, *renderer, nothing, level, "the sky");
        const int worst = worst_gradient(open, level, sky, false);
        expect(worst <= 2, "every sampled pixel matches the gradient for its view direction (worst " + std::to_string(worst) + ")");
        const Rgb top = pixel(open, cx, 2);
        const Rgb bottom = pixel(open, cx, height - 3);
        expect(furthest(bottom, bytes(sky.horizon)) <= 1 && top[2] < bottom[2] && top[0] < bottom[0] - 40,
               "below the horizon is the horizon colour, and upwards turns to the zenith: " + text(top) + " vs " + text(bottom));
        expect(renderer->last_draw_count() == 0 && renderer->last_draw_stats().draws == 0, "the sky is not counted as a draw");
        // Deliberate break: with the sky's draw removed the pixels are the clear colour, far from the gradient.
        expect(worst_gradient(blank, level, sky, false) > 100, "without the sky's draw the gradient check fails");

        // Tilted and turned, the same formula holds: the direction comes from the camera, not the screen.
        const render::Camera tilted{{50.0, 3.0, -20.0}, {53.0, 5.0, -18.0}};
        render::Sky no_sun = sky;
        no_sun.sun_radius = 0.F;
        expect(renderer->enable_sky(sky_shaders, no_sun, error), "the sky without a sun enables: " + error.message);
        const int worst_tilted = worst_gradient(draw(*device, *renderer, nothing, tilted, "a tilted view"), tilted, no_sun, false);
        expect(worst_tilted <= 2, "a tilted view far from the origin matches too (worst " + std::to_string(worst_tilted) + ")");

        // The sun sits where the light comes from.
        const render::Lighting lighting = renderer->lighting();
        const math::Vec3 to_sun = math::normalize(lighting.direction);
        const render::Camera sunward{{0.0, 0.0, 0.0}, {to_sun.x, to_sun.y, to_sun.z}};
        const Rgb no_disc = pixel(draw(*device, *renderer, nothing, sunward, "towards the light, no sun"), cx, cy);
        expect(furthest(no_disc, bytes(gradient(no_sun, to_sun))) <= 2, "with sun_radius 0 there is no sun: " + text(no_disc));
        render::Sky with_sun = sky;
        with_sun.sun = {1.F, 0.5F, 0.F};
        with_sun.sun_radius = 0.05F;
        expect(renderer->enable_sky(sky_shaders, with_sun, error), "the sky with a sun enables: " + error.message);
        const lykoware::Image sunny = draw(*device, *renderer, nothing, sunward, "towards the light");
        expect(pixel(sunny, cx, cy) == Rgb{255, 128, 0}, "looking at the light, the centre is the sun: " + text(pixel(sunny, cx, cy)));
        expect(furthest(pixel(sunny, 4, 4), bytes(gradient(with_sun, direction(sunward, 4, 4)))) <= 2,
               "away from the disc the sky is the gradient");
        render::Lighting moved = lighting;
        moved.direction = {-1.F, 0.2F, 0.F};
        expect(renderer->set_lighting(moved, error), "the light moves: " + error.message);
        const Rgb after = pixel(draw(*device, *renderer, nothing, sunward, "after the light moved"), cx, cy);
        expect(furthest(after, bytes(gradient(with_sun, to_sun))) <= 2, "the sun follows the light: " + text(after));
        expect(renderer->set_lighting(lighting, error), "the light returns");

        // The scene draws over the sky, and is itself unchanged.
        const lykoware::Image cube_sky = draw(*device, *renderer, cube, level, "the cube under the sky");
        expect(pixel(cube_sky, cx, cy) == pixel(cube_alone, cx, cy) && pixel(cube_alone, cx, cy)[0] > 100,
               "the cube covers the sky and keeps its colour: " + text(pixel(cube_sky, cx, cy)));
        expect(pixel(cube_sky, 4, 4) == pixel(open, 4, 4), "and the sky shows around it");
        expect(renderer->last_draw_count() == 1 && renderer->last_draw_stats().draws == 1 && renderer->last_draw_stats().batches == 1,
               "the cube is the one counted draw");

        // Through the HDR target a sun brighter than 1 survives to the tone map.
        render::Sky bright = with_sun;
        bright.sun = {4.F, 4.F, 1.F};
        if (renderer->enable_sky(sky_shaders, bright, error) && renderer->enable_post({spirv[4], spirv[5]}, {}, error)) {
            const lykoware::Image mapped = draw(*device, *renderer, nothing, level, "the sky with post");
            const int worst_mapped = worst_gradient(mapped, level, bright, true);
            expect(worst_mapped <= 2, "with post-processing the sky is the tone-mapped gradient (worst " + std::to_string(worst_mapped) + ")");
            const Rgb disc = pixel(draw(*device, *renderer, nothing, sunward, "the bright sun with post"), cx, cy);
            expect(furthest(disc, bytes(reinhard(bright.sun))) <= 2, "a sun of 4 tone-maps to 0.8, not clipped: " + text(disc));
            renderer->disable_post();
        } else {
            expect(false, "the bright sky and post enable: " + error.message);
        }

        // Refusals leave the sky as it was.
        const auto refused = [&](const render::Sky& bad) {
            return !renderer->enable_sky(sky_shaders, bad, error) && error.failure == rhi::RhiFailure::InvalidArgument
                && renderer->sky() && renderer->sky()->sun.x == 4.F;
        };
        render::Sky bad = sky;
        bad.zenith.y = std::numeric_limits<float>::quiet_NaN();
        expect(refused(bad), "a NaN colour is refused");
        bad = sky;
        bad.horizon.x = -0.1F;
        expect(refused(bad), "a negative colour is refused");
        bad = sky;
        bad.sun_radius = 0.6F;
        expect(refused(bad), "a sun_radius past the limit is refused");

        renderer->disable_sky();
        const lykoware::Image off = draw(*device, *renderer, nothing, level, "the sky disabled");
        expect(!renderer->sky() && same(off, blank), "disabling the sky draws exactly as before it was enabled");
        expect(same(draw(*device, *renderer, cube, level, "the cube, sky disabled"), cube_alone), "with a scene too");
    }
    {
        auto skinned = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, spirv[0], spirv[1], error, spirv[6]);
        expect(skinned && !skinned->enable_sky(sky_shaders, {}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "a skinned renderer refuses the sky");
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.sky: the gradient, the sun, the scene over the sky and the tone-mapped sky pass\n";
    return EXIT_SUCCESS;
}
