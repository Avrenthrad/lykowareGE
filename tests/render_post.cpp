// render.post (ADR 0087): tone mapping after a 16-bit float scene target. Three cubes whose lit
// colour is linear 0.5, 2 and 4 stand before a white wall, all facing a light that points at the
// camera. With post-processing each must match post.slang's Reinhard tone map within 2 of 255 at
// exposures 1 and 2. Deliberate breaks: without it, 2 and 4 both clip to 255, and with it they
// must stay more than 20 apart, which an 8-bit scene target could not do; a half-transparent pane
// enabled later must blend in float before the tone map; off and disabled again are byte-identical.
// Bad exposures, a missing shader, a skinned renderer and an 8-bit pipeline on the HDR target are
// refused.
//
// Usage: lykoware_render_post_test LIT.vert LIT.frag POST.vert POST.frag TRANSPARENT.vert
//        TRANSPARENT.frag MATERIAL.vert MATERIAL.frag MATERIAL_SHADOWED.vert MATERIAL_SHADOWED.frag
//        SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag SKINNED.vert
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device). A software
// GPU without Vulkan skips; it never passes.

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/hdr_target.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <array>
#include <cmath>
#include <cstddef>
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
const render::Camera camera{{0.0, 0.0, 6.0}, {0.0, 0.0, 0.0}};
// Towards the camera: every front face is fully lit, so a surface shows its colour.
const render::Lighting lighting{{0.F, 0.F, 1.F}, 0.25F};

// Front-face centres of the three cubes (front at z = 0.5), the wall, and the pane.
const math::DVec3 dim_at{-1.5, 0.0, 0.5};
const math::DVec3 bright_at{0.0, 0.0, 0.5};
const math::DVec3 brighter_at{1.5, 0.0, 0.5};
const math::DVec3 wall_at{0.0, -1.8, -1.9};
const math::DVec3 pane_at{0.0, 1.6, 1.05};

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

using Rgb = std::array<int, 3>;

Rgb rgb_at(const lykoware::Image& image, math::DVec3 point)
{
    const auto xy = pixel_of(point);
    const std::size_t at = (static_cast<std::size_t>(xy.second) * image.width() + xy.first) * 3;
    return {image.pixels()[at], image.pixels()[at + 1], image.pixels()[at + 2]};
}

// post.slang's tone map of a linear grey or colour, as 8-bit.
Rgb tone(math::Vec3 linear, float exposure)
{
    const float c[3] = {linear.x, linear.y, linear.z};
    Rgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float e = c[i] * exposure;
        out[i] = static_cast<int>(std::lround(e / (1.F + e) * 255.F));
    }
    return out;
}

Rgb grey(int value)
{
    return {value, value, value};
}

bool near(const Rgb& got, const Rgb& want, int tolerance)
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(got[i] - want[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

std::string text(const Rgb& c)
{
    return "(" + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", " + std::to_string(c[2]) + ")";
}

struct Scene {
    bool pane = false;
};

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const Scene& scene, rhi::RhiError& error,
                     bool* drew = nullptr)
{
    error = {};
    ecs::World entities;
    const auto add = [&](math::DVec3 position, math::Vec3 scale, math::Vec3 colour, float opacity) {
        const ecs::Entity entity = entities.create();
        lykoware::Transform at;
        at.position = position;
        entities.add<lykoware::Transform>(entity, at);
        render::MeshRenderer mesh{render::MeshShape::Cube, scale, colour};
        mesh.opacity = opacity;
        entities.add<render::MeshRenderer>(entity, mesh);
    };
    add({0.0, 0.0, -2.0}, {14.F, 14.F, 0.2F}, {1.F, 1.F, 1.F}, 1.F);
    // Linear 0.5, 2 and 4 once lit: an 8-bit target keeps only the first apart.
    add({-1.5, 0.0, 0.0}, {1.F, 1.F, 1.F}, {0.5F, 0.5F, 0.5F}, 1.F);
    add({0.0, 0.0, 0.0}, {1.F, 1.F, 1.F}, {2.F, 2.F, 2.F}, 1.F);
    add({1.5, 0.0, 0.0}, {1.F, 1.F, 1.F}, {4.F, 4.F, 4.F}, 1.F);
    if (scene.pane) {
        add({0.0, 1.6, 1.0}, {0.6F, 0.6F, 0.1F}, {1.F, 0.F, 0.F}, 0.5F);
    }
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const bool ok = target && renderer.draw(entities, camera, *target, sky, error);
    if (drew != nullptr) {
        *drew = ok;
    }
    lykoware::Image image;
    if (ok && !target->read_back(image, error)) {
        std::cerr << "read failed: " << error.message << '\n';
    }
    return image;
}

lykoware::Image draw_checked(rhi::Device& device, render::Renderer& renderer, const Scene& scene, std::string_view name)
{
    rhi::RhiError error;
    lykoware::Image image = draw(device, renderer, scene, error);
    expect(image.width() == width, std::string(name) + ": renders (" + error.message + ")");
    return image;
}

bool same(const lykoware::Image& a, const lykoware::Image& b, std::uint8_t tolerance)
{
    return a.width() == width && b.width() == width && lykoware::compare_images(a, b, tolerance).matches;
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

// Checks the three cubes and the wall against the tone map at `exposure`.
void check_tone(const lykoware::Image& image, float exposure, std::string_view name)
{
    if (image.width() != width) {
        return;
    }
    const Rgb dim = rgb_at(image, dim_at);
    const Rgb bright = rgb_at(image, bright_at);
    const Rgb brighter = rgb_at(image, brighter_at);
    const Rgb wall = rgb_at(image, wall_at);
    std::cout << name << ": 0.5 " << text(dim) << ", 2 " << text(bright) << ", 4 " << text(brighter) << ", wall "
              << text(wall) << '\n';
    expect(near(dim, tone({0.5F, 0.5F, 0.5F}, exposure), 2), std::string(name) + ": linear 0.5 is tone-mapped");
    expect(near(bright, tone({2.F, 2.F, 2.F}, exposure), 2), std::string(name) + ": linear 2 is tone-mapped");
    expect(near(brighter, tone({4.F, 4.F, 4.F}, exposure), 2), std::string(name) + ": linear 4 is tone-mapped");
    expect(near(wall, tone({1.F, 1.F, 1.F}, exposure), 2), std::string(name) + ": the wall is tone-mapped");
    expect(brighter[0] > bright[0] + 20, std::string(name) + ": values above 1 stay apart");
}

}

int main(int argc, char* argv[])
{
    if (argc != 15) {
        std::cerr << "usage: lykoware_render_post_test LIT.vert LIT.frag POST.vert POST.frag TRANSPARENT.vert "
                     "TRANSPARENT.frag MATERIAL.vert MATERIAL.frag MATERIAL_SHADOWED.vert MATERIAL_SHADOWED.frag "
                     "SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag SKINNED.vert\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.post test";
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
        std::vector<std::vector<std::uint32_t>> code;
        for (int index = 1; index < argc; ++index) {
            code.push_back(words(argv[index]));
        }
        if (g_failures != 0) {
            return EXIT_FAILURE;
        }
        const auto& lit_vertex = code[0];
        const auto& lit_fragment = code[1];
        const render::PostShaders shaders{code[2], code[3]};
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        expect(renderer->set_lighting(lighting, error), "the light is accepted");

        // Off: straight into 8 bits, so 2 and 4 both clip to white.
        const lykoware::Image plain = draw_checked(*device, *renderer, {}, "off");
        if (plain.width() == width) {
            expect(near(rgb_at(plain, dim_at), grey(128), 2) && rgb_at(plain, bright_at) == grey(255)
                       && rgb_at(plain, brighter_at) == grey(255),
                   "without post-processing linear 2 and 4 both clip to 255");
        }
        expect(!renderer->post(), "post-processing starts off");

        // Refusals leave it off.
        for (const float bad : {0.F, -1.F, 100.F, std::numeric_limits<float>::quiet_NaN()}) {
            error = {};
            expect(!renderer->enable_post(shaders, {bad}, error) && error.failure == rhi::RhiFailure::InvalidArgument
                       && !renderer->post(),
                   "an exposure of " + std::to_string(bad) + " is refused");
        }
        error = {};
        expect(!renderer->enable_post({{}, code[3]}, {}, error) && !renderer->post(),
               "a missing post vertex shader is refused");

        if (!renderer->enable_post(shaders, {}, error)) {
            const bool unavailable = error.failure == rhi::RhiFailure::NoSuitableDevice;
            std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
            return unavailable ? exit_skip : EXIT_FAILURE;
        }
        bool drew = false;
        const lykoware::Image mapped = draw(*device, *renderer, {}, error, &drew);
        if (!drew && error.failure == rhi::RhiFailure::NoSuitableDevice) {
            std::cout << "SKIP: " << error.message << '\n';
            return exit_skip;
        }
        expect(drew, "post, exposure 1: renders (" + error.message + ")");
        check_tone(mapped, 1.F, "exposure 1");

        error = {};
        expect(!renderer->enable_post(shaders, {-2.F}, error) && renderer->post() && renderer->post()->exposure == 1.F,
               "a refused exposure leaves the settings unchanged");
        expect(renderer->enable_post(shaders, {2.F}, error), "exposure 2 is accepted: " + error.message);
        check_tone(draw_checked(*device, *renderer, {}, "exposure 2"), 2.F, "exposure 2");

        // Enabled after post: transparency, materials and shadows are rebuilt for the float target.
        expect(renderer->enable_post(shaders, {1.F}, error), "exposure 1 again: " + error.message);
        expect(renderer->enable_transparency({code[4], code[5]}, error), "transparency is enabled: " + error.message);
        expect(renderer->enable_materials({code[6], code[7], code[8], code[9]}, error),
               "materials are enabled: " + error.message);
        expect(renderer->enable_shadows({code[10], code[11], code[12]}, {}, error), "shadows are enabled: " + error.message);
        const lykoware::Image mixed = draw_checked(*device, *renderer, {true}, "with transparency, materials, shadows");
        if (mixed.width() == width) {
            const Rgb pane = rgb_at(mixed, pane_at);
            std::cout << "pane " << text(pane) << '\n';
            // Half red over linear white is (1, 0.5, 0.5), blended in float, then tone-mapped.
            expect(near(pane, tone({1.F, 0.5F, 0.5F}, 1.F), 2), "the pane blends in float before the tone map");
            expect(near(rgb_at(mixed, bright_at), tone({2.F, 2.F, 2.F}, 1.F), 2),
                   "plain parts through the material pipeline keep values above 1");
        }
        renderer->disable_shadows();
        renderer->disable_materials();
        renderer->disable_transparency();

        // Disabled again: exactly the image without it.
        renderer->disable_post();
        expect(same(plain, draw_checked(*device, *renderer, {}, "disabled"), 0) && !renderer->post(),
               "disabling post-processing draws exactly as before it was enabled");

        // A skinned renderer refuses post-processing (a later slice).
        auto skinned = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error,
                                                code[13]);
        expect(skinned != nullptr, "the skinned renderer is created");
        if (skinned) {
            error = {};
            expect(!skinned->enable_post(shaders, {}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
                   "a skinned renderer refuses post-processing");
        }

        // The HDR target refuses a bad extent and a pipeline for another format.
        error = {};
        expect(!rhi::HdrTarget::create(*device, 0, 4, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "an empty HDR target is refused");
        auto hdr = rhi::HdrTarget::create(*device, 8, 8, error);
        const auto vertex = rhi::Shader::load(*device, lit_vertex, rhi::ShaderStage::Vertex, error);
        const auto fragment = rhi::Shader::load(*device, lit_fragment, rhi::ShaderStage::Fragment, error);
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
        desc.depth = true;
        desc.texture = true;
        desc.push_constant_bytes = rhi::push_constant_max_bytes;
        const auto eight_bit = rhi::GraphicsPipeline::create(*device, desc, error);
        expect(hdr && eight_bit, "the HDR fixture is created: " + error.message);
        if (hdr && eight_bit) {
            error = {};
            expect(!hdr->render(sky, *eight_bit, {}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
                   "an 8-bit pipeline is refused by the HDR target");
        }
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " post assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "render post passed\n";
    return EXIT_SUCCESS;
}
