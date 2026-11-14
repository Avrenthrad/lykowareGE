// render.shadows (07.03b, ADR 0084): a directional light's shadow map, cast and received.
//
// A white 1 m cube floats over a white ground slab under a light from above. With shadows on,
// the ground under the cube keeps only the ambient, the open ground and the cube's top stay
// fully lit, and a slanted light moves the shadow. With shadows off the image is byte for byte
// the unshadowed renderer's. Skips with exit code 77 when there is no Vulkan; it never passes
// without drawing.

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
const render::Camera camera{{0.0, 6.0, 6.0}, {0.0, 0.0, 0.0}};

// Points on the ground's top face (y = 0) and the floating cube's top face (y = 2).
const math::DVec3 under{0.0, 0.0, 0.0};
const math::DVec3 open_ground{2.5, 0.0, 2.5};
const math::DVec3 cube_top{0.0, 2.0, 0.0};
// Under a light along (1, 1, 0) the cube (y 1..2) shades x -2.5..-0.5 on the ground.
const math::DVec3 slanted_shade{-1.5, 0.0, 0.0};

std::pair<std::uint32_t, std::uint32_t> pixel_of(math::DVec3 point)
{
    const math::Mat4 view = math::look_at({}, math::relative_to(camera.target, camera.eye), camera.up);
    const math::Mat4 projection =
        math::perspective(camera.vertical_fov, static_cast<float>(width) / static_cast<float>(height), camera.near);
    const math::Vec3 p = math::relative_to(point, camera.eye);
    const math::Vec4 clip = projection * view * math::Vec4{p.x, p.y, p.z, 1.F};
    const float x = (clip.x / clip.w * 0.5F + 0.5F) * static_cast<float>(width);
    const float y = (clip.y / clip.w * 0.5F + 0.5F) * static_cast<float>(height);
    return {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
}

int red_at(const lykoware::Image& image, math::DVec3 point)
{
    const auto xy = pixel_of(point);
    return image.pixels()[(static_cast<std::size_t>(xy.second) * image.width() + xy.first) * 3];
}

int lit(float ambient, float diffuse)
{
    return static_cast<int>(std::lround((ambient + (1.F - ambient) * diffuse) * 255.F));
}

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, bool with_cube)
{
    rhi::RhiError error;
    ecs::World entities;
    const ecs::Entity ground = entities.create();
    lykoware::Transform ground_at;
    ground_at.position = {0.0, -0.05, 0.0};
    entities.add<lykoware::Transform>(ground, ground_at);
    entities.add<render::MeshRenderer>(ground, render::MeshRenderer{render::MeshShape::Cube, {8.F, 0.1F, 8.F}, {1.F, 1.F, 1.F}});
    if (with_cube) {
        const ecs::Entity cube = entities.create();
        lykoware::Transform cube_at;
        cube_at.position = {0.0, 1.5, 0.0};
        entities.add<lykoware::Transform>(cube, cube_at);
        entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F}});
    }
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    if (!target || !renderer.draw(entities, camera, *target, sky, error)) {
        std::cerr << "draw failed: " << error.message << '\n';
        return {};
    }
    lykoware::Image image;
    if (!target->read_back(image, error)) {
        std::cerr << "read failed: " << error.message << '\n';
    }
    return image;
}

bool near(int actual, int want, std::string_view what)
{
    std::cout << what << ": " << actual << " (want " << want << ")\n";
    return std::abs(actual - want) <= 2;
}

bool read(const char* path, std::vector<std::uint32_t>& words)
{
    rhi::RhiError error;
    if (!rhi::read_spirv_file(path, words, error)) {
        std::cerr << "FAIL: " << path << " loads: " << error.message << '\n';
        return false;
    }
    return true;
}

}

int main(int argc, char* argv[])
{
    if (argc != 7) {
        std::cerr << "usage: lykoware_render_shadows_test LIT.vert LIT.frag SHADOW.vert LIT_SHADOWED.vert "
                     "LIT_SHADOWED.frag SKINNED.vert\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.shadows test";
    instance_config.validation = rhi::Validation::IfAvailable;
    rhi::RhiError error;
    auto instance = rhi::Instance::create(instance_config, error);
    if (!instance) {
        const bool unavailable = error.failure == rhi::RhiFailure::NoLoader
            || error.failure == rhi::RhiFailure::NoDriver || error.failure == rhi::RhiFailure::VersionTooOld;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }
    auto device = rhi::Device::create(*instance, {}, error);
    if (!device) {
        const bool unavailable = error.failure == rhi::RhiFailure::NoSuitableDevice;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }
    std::cout << "device: " << device->info().name << '\n';
    {
        std::vector<std::uint32_t> lit_vertex;
        std::vector<std::uint32_t> lit_fragment;
        std::vector<std::uint32_t> caster;
        std::vector<std::uint32_t> receiver_vertex;
        std::vector<std::uint32_t> receiver_fragment;
        std::vector<std::uint32_t> skinned_vertex;
        if (!read(argv[1], lit_vertex) || !read(argv[2], lit_fragment) || !read(argv[3], caster)
            || !read(argv[4], receiver_vertex) || !read(argv[5], receiver_fragment) || !read(argv[6], skinned_vertex)) {
            return EXIT_FAILURE;
        }
        const render::ShadowShaders shaders{caster, receiver_vertex, receiver_fragment};
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        const float ambient = renderer->lighting().ambient;
        expect(renderer->set_lighting({{0.F, 1.F, 0.F}, ambient}, error), "a light from above is accepted");

        // Off: no shadow, and the image the unshadowed renderer always drew.
        expect(!renderer->shadows().has_value(), "shadows start off");
        const lykoware::Image before = draw(*device, *renderer, true);
        expect(before.width() == width, "the unshadowed scene renders");
        if (before.width() == width) {
            expect(near(red_at(before, under), lit(ambient, 1.F), "off, under the cube"),
                   "without shadows the ground under the cube is fully lit");
        }
        expect(renderer->last_shadow_caster_count() == 0, "no casters are drawn while shadows are off");

        // On.
        render::Shadows settings;
        settings.resolution = 1024;
        settings.half_extent = 8.F;
        settings.depth_range = 32.F;
        expect(renderer->enable_shadows(shaders, settings, error), "shadows are enabled: " + error.message);
        expect(renderer->shadows().has_value() && renderer->shadows()->resolution == 1024, "the settings are kept");
        const lykoware::Image shadowed = draw(*device, *renderer, true);
        expect(shadowed.width() == width, "the shadowed scene renders");
        if (shadowed.width() == width) {
            expect(near(red_at(shadowed, under), lit(ambient, 0.F), "on, under the cube"),
                   "the ground under the cube keeps only the ambient");
            expect(near(red_at(shadowed, open_ground), lit(ambient, 1.F), "on, open ground"),
                   "the open ground stays fully lit");
            expect(near(red_at(shadowed, cube_top), lit(ambient, 1.F), "on, the cube's top"),
                   "the cube's top does not shadow itself");
        }
        expect(renderer->last_shadow_caster_count() == 2, "both the ground and the cube are drawn as casters");

        // The deliberate break: with the caster gone, the shadow goes with it.
        const lykoware::Image no_caster = draw(*device, *renderer, false);
        expect(no_caster.width() == width && near(red_at(no_caster, under), lit(ambient, 1.F), "on, no cube"),
               "without the cube nothing shades the ground");

        // A slanted light moves the shadow off the spot under the cube.
        expect(renderer->set_lighting({{1.F, 1.F, 0.F}, ambient}, error), "a slanted light is accepted");
        const lykoware::Image slanted = draw(*device, *renderer, true);
        const float slant = 1.F / std::sqrt(2.F);
        expect(slanted.width() == width && near(red_at(slanted, slanted_shade), lit(ambient, 0.F), "slanted, shade"),
               "a slanted light throws the shadow sideways");
        expect(slanted.width() == width && near(red_at(slanted, under), lit(ambient, slant), "slanted, under"),
               "a slanted light leaves the spot under the cube lit");
        expect(renderer->set_lighting({{0.F, 1.F, 0.F}, ambient}, error), "the light from above is restored");

        // Off again: byte for byte the image from before shadows were enabled.
        renderer->disable_shadows();
        expect(!renderer->shadows().has_value(), "disable_shadows turns them off");
        const lykoware::Image after = draw(*device, *renderer, true);
        expect(before.width() == width && after.width() == width
                   && lykoware::compare_images(before, after, 0).matches,
               "with shadows off the renderer draws exactly as before");

        // Refusals leave shadows as they were (off here).
        const float nan = std::numeric_limits<float>::quiet_NaN();
        auto with = [&](auto change) {
            render::Shadows s;
            change(s);
            return s;
        };
        const std::pair<render::Shadows, const char*> bad[] = {
            {with([](render::Shadows& s) { s.resolution = 8; }), "a resolution below the minimum"},
            {with([](render::Shadows& s) { s.resolution = 9000; }), "a resolution above the maximum"},
            {with([](render::Shadows& s) { s.half_extent = 0.F; }), "a zero extent"},
            {with([&](render::Shadows& s) { s.half_extent = nan; }), "a NaN extent"},
            {with([](render::Shadows& s) { s.depth_range = -1.F; }), "a negative range"},
            {with([](render::Shadows& s) { s.bias = -0.1F; }), "a negative bias"},
            {with([](render::Shadows& s) { s.bias = s.depth_range; }), "a bias as deep as the range"},
        };
        for (const auto& [shadows, why] : bad) {
            error = {};
            expect(!renderer->enable_shadows(shaders, shadows, error) && error.failure == rhi::RhiFailure::InvalidArgument,
                   std::string(why) + " is refused");
            expect(!renderer->shadows().has_value(), std::string(why) + " leaves shadows off");
        }
        error = {};
        expect(!renderer->enable_shadows({{}, receiver_vertex, receiver_fragment}, settings, error)
                   && error.failure != rhi::RhiFailure::None,
               "a missing caster shader is refused");
        expect(!renderer->shadows().has_value(), "a missing shader leaves shadows off");

        auto skinned = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error,
                                                skinned_vertex);
        expect(skinned != nullptr, "the skinned renderer is created");
        if (skinned) {
            error = {};
            expect(!skinned->enable_shadows(shaders, settings, error) && error.failure == rhi::RhiFailure::InvalidArgument,
                   "a skinned renderer refuses shadows for now");
        }
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " shadow assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "render shadows passed\n";
    return EXIT_SUCCESS;
}
