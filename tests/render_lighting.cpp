// render.lighting (ADR 0082): the scene's stated directional light and ambient. A white cube is
// drawn under several lights; the centres of its top and front faces must match
// ambient + (1 - ambient) * max(0, n . light) within 2 of 255. Pixels are read well inside each
// face, so edge rasterisation does not matter.
//
// Usage: lykoware_render_lighting_test LIT_VERTEX.spv LIT_FRAGMENT.spv
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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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
const render::Camera camera{{0.0, 2.5, 3.0}, {0.0, 0.0, 0.0}};

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

// What the shader should give a white face with unit normal n.
int expected(const render::Lighting& lighting, math::Vec3 n)
{
    const math::Vec3 d = lighting.direction;
    const float length = std::sqrt(math::dot(d, d));
    const float diffuse = std::max(0.F, math::dot(n, math::Vec3{d.x / length, d.y / length, d.z / length}));
    return static_cast<int>(std::lround((lighting.ambient + (1.F - lighting.ambient) * diffuse) * 255.F));
}

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer)
{
    rhi::RhiError error;
    ecs::World entities;
    const ecs::Entity cube = entities.create();
    entities.add<lykoware::Transform>(cube, lykoware::Transform{});
    entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F}});
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

const math::DVec3 top{0.0, 0.5, 0.0};
const math::DVec3 front{0.0, 0.0, 0.5};

// Draws under `lighting` and checks both faces against the formula.
lykoware::Image check_light(rhi::Device& device, render::Renderer& renderer, const render::Lighting& lighting,
                            const std::string& name)
{
    rhi::RhiError error;
    expect(renderer.set_lighting(lighting, error), name + ": accepted");
    lykoware::Image image = draw(device, renderer);
    expect(image.width() == width, name + ": renders");
    if (image.width() != width) {
        return image;
    }
    const int t = red_at(image, top);
    const int f = red_at(image, front);
    std::cout << name << ": top " << t << " (want " << expected(lighting, {0, 1, 0}) << "), front " << f << " (want "
              << expected(lighting, {0, 0, 1}) << ")\n";
    expect(std::abs(t - expected(lighting, {0, 1, 0})) <= 2, name + ": the top face follows the light");
    expect(std::abs(f - expected(lighting, {0, 0, 1})) <= 2, name + ": the front face follows the light");
    return image;
}

}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "usage: lykoware_render_lighting_test LIT_VERTEX.spv LIT_FRAGMENT.spv\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.lighting test";
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
        std::vector<std::uint32_t> vertex_words;
        std::vector<std::uint32_t> fragment_words;
        if (!rhi::read_spirv_file(argv[1], vertex_words, error) || !rhi::read_spirv_file(argv[2], fragment_words, error)) {
            std::cerr << "FAIL: the lit shaders load: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex_words, fragment_words, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }

        // A renderer never given lighting draws exactly as with the default Lighting.
        const lykoware::Image untouched = draw(*device, *renderer);
        const render::Lighting defaults;
        const lykoware::Image stated = check_light(*device, *renderer, defaults, "default");
        expect(untouched.width() == width && stated.width() == width
                   && lykoware::compare_images(untouched, stated, 0).matches,
               "the default lighting is the first renderer's fixed light, byte for byte");

        const lykoware::Image above = check_light(*device, *renderer, {{0.F, 1.F, 0.F}, 0.25F}, "from above");
        const lykoware::Image ahead = check_light(*device, *renderer, {{0.F, 0.F, 2.F}, 0.25F}, "from in front");
        const lykoware::Image below = check_light(*device, *renderer, {{0.F, -1.F, 0.F}, 0.F}, "from below, no ambient");
        check_light(*device, *renderer, {{0.F, 1.F, 1.F}, 0.6F}, "diagonal, strong ambient");

        // Deliberate break: a renderer that ignored its stated light would draw these the same.
        expect(above.width() == width && ahead.width() == width
                   && !lykoware::compare_images(above, ahead, 2).matches,
               "moving the light changes the image");
        expect(below.width() == width && red_at(below, top) == 0 && red_at(below, front) == 0,
               "a light from below with no ambient leaves the top and front black");

        // Refusals leave the lighting as it was.
        const render::Lighting kept = renderer->lighting();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::pair<render::Lighting, const char*> bad[] = {
            {{{0.F, 0.F, 0.F}, 0.25F}, "a zero direction"},
            {{{nan, 1.F, 0.F}, 0.25F}, "a NaN direction"},
            {{{0.F, 1.F, 0.F}, -0.1F}, "an ambient below 0"},
            {{{0.F, 1.F, 0.F}, 1.1F}, "an ambient above 1"},
            {{{0.F, 1.F, 0.F}, nan}, "a NaN ambient"},
        };
        for (const auto& [lighting, why] : bad) {
            error = {};
            expect(!renderer->set_lighting(lighting, error) && error.failure == rhi::RhiFailure::InvalidArgument,
                   std::string(why) + " is refused");
            expect(renderer->lighting().direction.y == kept.direction.y && renderer->lighting().ambient == kept.ambient,
                   std::string(why) + " leaves the lighting unchanged");
        }
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " lighting assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "render lighting passed\n";
    return EXIT_SUCCESS;
}
