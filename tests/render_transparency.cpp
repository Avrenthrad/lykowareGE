// render.transparency (ADR 0086): transparent surfaces, sorted far to near and blended. Two
// half-transparent panes, red behind blue, stand in front of an opaque white wall, all facing a
// light that points at the camera, so every face is fully lit. Each region's colour must match a
// CPU blend of the panes in far-to-near order within 2 of 255. Deliberate breaks: the panes are
// added near-first and far-first and must draw the same image (unsorted drawing would differ by
// more than 60 in the overlap); a renderer without transparency shaders, and one whose
// transparency is disabled again, draws them opaque exactly as before; an opacity of 0 leaves the
// wall untouched; a pane behind the wall is hidden by the depth test. Bad opacities, a missing
// shader, a skinned renderer and RHI draws whose own pipeline does not fit the pass are refused.
//
// Usage: lykoware_render_transparency_test LIT.vert LIT.frag TRANSPARENT.vert TRANSPARENT.frag
//        MATERIAL.vert MATERIAL.frag MATERIAL_SHADOWED.vert MATERIAL_SHADOWED.frag SHADOW.vert
//        LIT_SHADOWED.vert LIT_SHADOWED.frag SKINNED.vert
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device). A software
// GPU without Vulkan skips; it never passes.

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/rhi/texture.hpp>

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

// Points on the front faces: the far (red) pane's front is z = 0.05, the near (blue) one's 1.05.
const math::DVec3 overlap{0.0, 0.0, 1.05};
const math::DVec3 red_only{-1.0, 0.0, 0.05};
const math::DVec3 blue_only{1.0, 0.0, 1.05};
const math::DVec3 wall_only{0.0, 1.6, -1.9};

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

// One straight-alpha blend into an 8-bit target: src * a + dst * (1 - a), stored rounded.
Rgb over(const Rgb& dst, const Rgb& src, float alpha)
{
    Rgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float value = static_cast<float>(src[i]) * alpha + static_cast<float>(dst[i]) * (1.F - alpha);
        out[i] = static_cast<int>(std::lround(value));
    }
    return out;
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

int distance(const Rgb& a, const Rgb& b)
{
    int most = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        most = std::max(most, std::abs(a[i] - b[i]));
    }
    return most;
}

std::string text(const Rgb& c)
{
    return "(" + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", " + std::to_string(c[2]) + ")";
}

struct Pane {
    double x = 0.0;
    double z = 0.0;
    math::Vec3 colour{};
    float opacity = 1.F;
};

struct Scene {
    std::vector<Pane> panes;
};

const Pane red_pane{-0.4, 0.0, {1.F, 0.F, 0.F}, 0.5F};
const Pane blue_pane{0.4, 1.0, {0.F, 0.F, 1.F}, 0.5F};

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const Scene& scene, rhi::RhiError& error,
                     bool* drew = nullptr)
{
    error = {};
    ecs::World entities;
    const ecs::Entity wall = entities.create();
    lykoware::Transform wall_at;
    wall_at.position = {0.0, 0.0, -2.0};
    entities.add<lykoware::Transform>(wall, wall_at);
    render::MeshRenderer wall_mesh{render::MeshShape::Cube, {12.F, 12.F, 0.2F}, {1.F, 1.F, 1.F}};
    entities.add<render::MeshRenderer>(wall, wall_mesh);
    for (const Pane& pane : scene.panes) {
        const ecs::Entity entity = entities.create();
        lykoware::Transform at;
        at.position = {pane.x, 0.0, pane.z};
        entities.add<lykoware::Transform>(entity, at);
        render::MeshRenderer mesh{render::MeshShape::Cube, {1.6F, 1.6F, 0.1F}, pane.colour};
        mesh.opacity = pane.opacity;
        entities.add<render::MeshRenderer>(entity, mesh);
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

// RHI misuse: a draw whose own pipeline does not fit the pass is refused before anything is
// submitted, and a blended depth-only pipeline is refused at creation.
void check_rhi_refusals(rhi::Device& device, std::span<const std::uint32_t> vertex_code,
                        std::span<const std::uint32_t> fragment_code)
{
    rhi::RhiError error;
    const auto vertex = rhi::Shader::load(device, vertex_code, rhi::ShaderStage::Vertex, error);
    const auto fragment = rhi::Shader::load(device, fragment_code, rhi::ShaderStage::Fragment, error);
    expect(vertex && fragment, "the transparent shaders load: " + error.message);
    if (!vertex || !fragment) {
        return;
    }
    rhi::GraphicsPipelineDesc desc;
    desc.vertex = vertex.get();
    desc.fragment = fragment.get();
    desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
    desc.depth = true;
    desc.texture = true;
    desc.push_constant_bytes = 144;
    desc.blend = rhi::BlendMode::Alpha;
    desc.depth_write = false;
    const auto pass = rhi::GraphicsPipeline::create(device, desc, error);
    expect(pass && pass->blend() == rhi::BlendMode::Alpha && !pass->depth_write(),
           "a blended pipeline that tests depth without writing it is created: " + error.message);
    desc.color_format = rhi::ColorFormat::Bgra8Unorm;
    const auto other_format = rhi::GraphicsPipeline::create(device, desc, error);
    desc.color_format = rhi::ColorFormat::Rgba8Unorm;
    desc.depth = false;
    const auto no_depth = rhi::GraphicsPipeline::create(device, desc, error);
    expect(other_format && no_depth, "the mismatched pipelines are created: " + error.message);

    rhi::GraphicsPipelineDesc depth_only;
    depth_only.vertex = vertex.get();
    depth_only.vertex_layout = rhi::VertexLayout::PositionNormalUv;
    depth_only.depth = true;
    depth_only.depth_only = true;
    depth_only.blend = rhi::BlendMode::Alpha;
    error = {};
    expect(!rhi::GraphicsPipeline::create(device, depth_only, error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a blended depth-only pipeline is refused");
    if (!pass || !other_format || !no_depth) {
        return;
    }

    const float vertices[3][8] = {{-1.F, -1.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F},
                                  {1.F, -1.F, 0.F, 0.F, 0.F, 1.F, 1.F, 0.F},
                                  {0.F, 1.F, 0.F, 0.F, 0.F, 1.F, 0.5F, 1.F}};
    const std::uint32_t indices[3] = {0, 1, 2};
    const auto vertex_buffer = rhi::Buffer::create(device, rhi::BufferUsage::Vertex, std::as_bytes(std::span(vertices)), error);
    const auto index_buffer = rhi::Buffer::create(device, rhi::BufferUsage::Index, std::as_bytes(std::span(indices)), error);
    rhi::TextureDesc white;
    white.width = 1;
    white.height = 1;
    white.mipmaps = false;
    const std::byte texel[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    const auto texture = rhi::Texture::create(device, white, texel, error);
    auto target = rhi::RenderTarget::create(device, {16, 16, true}, error);
    expect(vertex_buffer && index_buffer && texture && target, "the RHI fixture is created: " + error.message);
    if (!vertex_buffer || !index_buffer || !texture || !target) {
        return;
    }
    std::array<std::byte, 144> constants{};
    rhi::MeshDraw draw;
    draw.vertices = vertex_buffer.get();
    draw.indices = index_buffer.get();
    draw.push_constants = constants;
    draw.texture = texture.get();
    expect(target->draw_meshes(sky, *pass, std::span(&draw, 1), error), "a fitting draw is accepted: " + error.message);
    for (const auto& [pipeline, name] : {std::pair{other_format.get(), "another colour format"},
                                         std::pair{no_depth.get(), "no depth in a depth pass"}}) {
        draw.pipeline = pipeline;
        error = {};
        expect(!target->draw_meshes(sky, *pass, std::span(&draw, 1), error)
                   && error.failure == rhi::RhiFailure::InvalidArgument,
               std::string("a draw's own pipeline with ") + name + " is refused");
    }
}

}

int main(int argc, char* argv[])
{
    if (argc != 13) {
        std::cerr << "usage: lykoware_render_transparency_test LIT.vert LIT.frag TRANSPARENT.vert TRANSPARENT.frag "
                     "MATERIAL.vert MATERIAL.frag MATERIAL_SHADOWED.vert MATERIAL_SHADOWED.frag SHADOW.vert "
                     "LIT_SHADOWED.vert LIT_SHADOWED.frag SKINNED.vert\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.transparency test";
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
        const auto lit_vertex = words(argv[1]);
        const auto lit_fragment = words(argv[2]);
        const auto transparent_vertex = words(argv[3]);
        const auto transparent_fragment = words(argv[4]);
        const auto material_vertex = words(argv[5]);
        const auto material_fragment = words(argv[6]);
        const auto material_shadowed_vertex = words(argv[7]);
        const auto material_shadowed_fragment = words(argv[8]);
        const auto caster_vertex = words(argv[9]);
        const auto receiver_vertex = words(argv[10]);
        const auto receiver_fragment = words(argv[11]);
        const auto skinned_vertex = words(argv[12]);
        if (g_failures != 0) {
            return EXIT_FAILURE;
        }
        const render::TransparencyShaders shaders{transparent_vertex, transparent_fragment};
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        expect(renderer->set_lighting(lighting, error), "the light is accepted");

        // Off: an opacity below 1 changes nothing until the renderer has transparency shaders.
        Pane opaque_red = red_pane;
        Pane opaque_blue = blue_pane;
        opaque_red.opacity = 1.F;
        opaque_blue.opacity = 1.F;
        const Scene sorted_scene{{red_pane, blue_pane}};
        const lykoware::Image opaque = draw_checked(*device, *renderer, {{opaque_red, opaque_blue}}, "opaque, off");
        const lykoware::Image ignored = draw_checked(*device, *renderer, sorted_scene, "half, off");
        expect(same(opaque, ignored, 0), "without transparency shaders a pane draws opaque, byte for byte");
        expect(!renderer->transparency_enabled() && renderer->last_transparent_draw_count() == 0,
               "transparency starts off");

        // A failed enable leaves transparency off.
        error = {};
        expect(!renderer->enable_transparency({{}, transparent_fragment}, error) && !renderer->transparency_enabled(),
               "a missing transparent vertex shader is refused and transparency stays off");

        if (!renderer->enable_transparency(shaders, error)) {
            const bool unavailable = error.failure == rhi::RhiFailure::NoSuitableDevice;
            std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
            return unavailable ? exit_skip : EXIT_FAILURE;
        }
        expect(renderer->transparency_enabled(), "transparency is on");

        // Fully opaque entities draw exactly as before.
        const lykoware::Image opaque_on = draw_checked(*device, *renderer, {{opaque_red, opaque_blue}}, "opaque, on");
        expect(same(opaque, opaque_on, 0) && renderer->last_transparent_draw_count() == 0,
               "an opacity of 1 draws exactly as with transparency off");

        // Far-first and near-first in ECS order: the same sorted image.
        const lykoware::Image far_first = draw_checked(*device, *renderer, sorted_scene, "far first");
        expect(renderer->last_transparent_draw_count() == 2 && renderer->last_draw_count() == 3,
               "two panes are drawn blended after the wall");
        const lykoware::Image near_first = draw_checked(*device, *renderer, {{blue_pane, red_pane}}, "near first");
        expect(same(far_first, near_first, 0), "the panes are sorted: ECS order does not change the image");

        const Rgb white{255, 255, 255};
        const Rgb red{255, 0, 0};
        const Rgb blue{0, 0, 255};
        const Rgb want_overlap = over(over(white, red, 0.5F), blue, 0.5F);
        const Rgb unsorted = over(over(white, blue, 0.5F), red, 0.5F);
        expect(distance(want_overlap, unsorted) > 60, "the fixture tells far-to-near from near-to-far");
        if (far_first.width() == width) {
            const Rgb got_overlap = rgb_at(far_first, overlap);
            const Rgb got_red = rgb_at(far_first, red_only);
            const Rgb got_blue = rgb_at(far_first, blue_only);
            const Rgb got_wall = rgb_at(far_first, wall_only);
            std::cout << "overlap " << text(got_overlap) << " (want " << text(want_overlap) << ", unsorted "
                      << text(unsorted) << "), red " << text(got_red) << ", blue " << text(got_blue) << ", wall "
                      << text(got_wall) << '\n';
            expect(near(got_overlap, want_overlap, 2), "the overlap is blue over red over the wall");
            expect(distance(got_overlap, unsorted) > 60, "the overlap is not red over blue");
            expect(near(got_red, over(white, red, 0.5F), 2), "red alone is half over the wall");
            expect(near(got_blue, over(white, blue, 0.5F), 2), "blue alone is half over the wall");
            expect(near(got_wall, white, 2), "the wall elsewhere is untouched");
        }

        // Opacity 0 leaves the wall exactly; a pane behind the wall is hidden by the depth test.
        const lykoware::Image wall = draw_checked(*device, *renderer, {}, "wall");
        Pane invisible = red_pane;
        invisible.opacity = 0.F;
        expect(same(wall, draw_checked(*device, *renderer, {{invisible}}, "invisible"), 0),
               "an opacity of 0 leaves what is behind untouched");
        Pane hidden = red_pane;
        hidden.z = -3.0;
        expect(same(wall, draw_checked(*device, *renderer, {{hidden}}, "hidden"), 0),
               "a pane behind the opaque wall is hidden by the depth test");

        // Bad opacities are refused while transparency is on.
        for (const float bad : {1.5F, -0.1F, std::numeric_limits<float>::quiet_NaN()}) {
            Pane pane = red_pane;
            pane.opacity = bad;
            bool drew = true;
            (void)draw(*device, *renderer, {{pane}}, error, &drew);
            expect(!drew && error.failure == rhi::RhiFailure::InvalidArgument,
                   "an opacity of " + std::to_string(bad) + " is refused");
        }

        // With materials and shadows on (the wall, without a Material, draws through the material
        // pipeline as lit), the panes still blend after a pipeline of another layout, and cast no
        // shadow on the wall.
        const render::MaterialShaders material_shaders{material_vertex, material_fragment, material_shadowed_vertex,
                                                       material_shadowed_fragment};
        expect(renderer->enable_materials(material_shaders, error), "materials are enabled: " + error.message);
        expect(renderer->enable_shadows({caster_vertex, receiver_vertex, receiver_fragment}, {}, error),
               "shadows are enabled: " + error.message);
        const lykoware::Image mixed = draw_checked(*device, *renderer, sorted_scene, "materials and shadows");
        if (mixed.width() == width) {
            const Rgb got = rgb_at(mixed, overlap);
            std::cout << "with materials and shadows: overlap " << text(got) << '\n';
            expect(near(got, want_overlap, 3), "the overlap blends the same with materials and shadows on");
            expect(renderer->last_shadow_caster_count() == 1, "only the opaque wall casts");
        }
        renderer->disable_shadows();
        renderer->disable_materials();

        // Disabled again: exactly the opaque image.
        renderer->disable_transparency();
        const lykoware::Image after = draw_checked(*device, *renderer, sorted_scene, "half, disabled");
        expect(same(opaque, after, 0) && !renderer->transparency_enabled()
                   && renderer->last_transparent_draw_count() == 0,
               "disabling transparency draws exactly as before it was enabled");

        // A skinned renderer refuses transparency (a later slice).
        auto skinned = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error,
                                                skinned_vertex);
        expect(skinned != nullptr, "the skinned renderer is created");
        if (skinned) {
            error = {};
            expect(!skinned->enable_transparency(shaders, error) && error.failure == rhi::RhiFailure::InvalidArgument
                       && !skinned->transparency_enabled(),
                   "a skinned renderer refuses transparency");
        }

        check_rhi_refusals(*device, transparent_vertex, transparent_fragment);
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " transparency assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "render transparency passed\n";
    return EXIT_SUCCESS;
}
