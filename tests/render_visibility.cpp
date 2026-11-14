// render.visibility (ADR 0088): culling, batching and freeing GPU resources.
//
// Batching: twelve small boxes, alternating between the built-in cube and an added box and between
// two textures, are drawn in ECS order with no two neighbours alike. Without batching the renderer
// reports twelve batches; with it, four (one per mesh and texture pair), the draw count stays
// twelve, and the image is byte-identical. The batched order must be the same on every draw.
//
// Culling: the same scene plus a cube behind the camera, a cube far to the left, a cube straddling
// the right edge of the view, and a cube behind the eye scaled so large that it holds the camera.
// With culling on, exactly the first two are culled, the counts come from the renderer, and the
// image is byte-identical to the unculled one (a too-eager cull would lose the straddling cube).
// A culled cube still casts its shadow.
//
// Resources: added meshes and textures show in gpu_resources(); freeing them returns the RHI's
// live buffer and texture counts to what the renderer alone holds, and destroying the renderer
// returns them to what they were before it. A texture a live mesh part names, a foreign id, and a
// second free are refused. A freed id draws nothing and is not handed out again. Deliberate
// break: a buffer held past the check shows up as a leak.
//
// Usage: lykoware_render_visibility_test LIT.vert LIT.frag SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device). A software
// GPU without Vulkan skips; it never passes.

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/rhi/texture.hpp>
#include <lykoware/transform.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
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
const render::Camera camera{{0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}};

// A 1 m box centred on the origin, the same shape as the built-in cube but its own mesh.
lykoware::MeshData box()
{
    lykoware::MeshData mesh;
    const std::array<math::Vec3, 6> normals = {{{1.F, 0.F, 0.F}, {-1.F, 0.F, 0.F}, {0.F, 1.F, 0.F},
                                                {0.F, -1.F, 0.F}, {0.F, 0.F, 1.F}, {0.F, 0.F, -1.F}}};
    for (const math::Vec3& n : normals) {
        // Two axes across the face, chosen so (u x v) points along n: counter-clockwise from outside.
        const math::Vec3 u = n.x != 0.F ? math::Vec3{0.F, n.x, 0.F} : (n.y != 0.F ? math::Vec3{0.F, 0.F, n.y} : math::Vec3{n.z, 0.F, 0.F});
        const math::Vec3 v = math::cross(n, u);
        const auto base = static_cast<std::uint32_t>(mesh.positions.size());
        for (const auto& [su, sv] : std::array<std::array<float, 2>, 4>{{{-1.F, -1.F}, {1.F, -1.F}, {1.F, 1.F}, {-1.F, 1.F}}}) {
            mesh.positions.push_back({(n.x + u.x * su + v.x * sv) * 0.5F, (n.y + u.y * su + v.y * sv) * 0.5F,
                                      (n.z + u.z * su + v.z * sv) * 0.5F});
            mesh.normals.push_back(n);
            mesh.uvs.push_back({0.F, 0.F});
        }
        for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
            mesh.indices.push_back(base + corner);
        }
    }
    return mesh;
}

struct Extra {
    math::DVec3 at{};
    math::Vec3 scale{1.F, 1.F, 1.F};
};

struct Setup {
    render::MeshId box{};
    std::array<render::TextureId, 2> textures{};
};

// Twelve boxes in a 4 x 3 grid in front of the camera, then the extras (plain cubes).
ecs::World scene(const Setup& setup, const std::vector<Extra>& extras)
{
    ecs::World entities;
    for (int i = 0; i < 12; ++i) {
        const ecs::Entity entity = entities.create();
        lykoware::Transform at;
        at.position = {-2.25 + 1.5 * (i % 4), -1.5 + 1.5 * (i / 4), 0.0};
        entities.add<lykoware::Transform>(entity, at);
        render::MeshRenderer mesh{render::MeshShape::Cube, {0.8F, 0.8F, 0.8F}, {1.F, 1.F, 1.F}};
        mesh.mesh = i % 2 == 0 ? render::MeshId{} : setup.box;
        mesh.texture = setup.textures[static_cast<std::size_t>((i / 2) % 2)];
        entities.add<render::MeshRenderer>(entity, mesh);
    }
    for (const Extra& extra : extras) {
        const ecs::Entity entity = entities.create();
        lykoware::Transform at;
        at.position = extra.at;
        entities.add<lykoware::Transform>(entity, at);
        entities.add<render::MeshRenderer>(entity, render::MeshRenderer{render::MeshShape::Cube, extra.scale, {0.9F, 0.6F, 0.3F}});
    }
    return entities;
}

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const ecs::World& entities, std::string_view name)
{
    rhi::RhiError error;
    lykoware::Image image;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const bool ok = target && renderer.draw(entities, camera, *target, sky, error) && target->read_back(image, error);
    expect(ok && image.width() == width, std::string(name) + ": renders (" + error.message + ")");
    return image;
}

bool same(const lykoware::Image& a, const lykoware::Image& b)
{
    return a.width() == width && b.width() == width && lykoware::compare_images(a, b, 0).matches;
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

std::optional<render::TextureId> solid(render::Renderer& renderer, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    lykoware::RgbaImage image;
    image.width = 1;
    image.height = 1;
    image.pixels = {r, g, b, 255};
    rhi::RhiError error;
    return renderer.add_texture(image, {}, error);
}

std::string stats_text(const render::DrawStats& s)
{
    return " (entities " + std::to_string(s.entities) + ", culled " + std::to_string(s.culled) + ", draws "
        + std::to_string(s.draws) + ", batches " + std::to_string(s.batches) + ")";
}

void batching(rhi::Device& device, render::Renderer& renderer, const Setup& setup)
{
    const ecs::World entities = scene(setup, {});
    expect(!renderer.visibility().batch && !renderer.visibility().cull, "visibility starts off");
    const lykoware::Image plain = draw(device, renderer, entities, "unbatched");
    const render::DrawStats unbatched = renderer.last_draw_stats();
    expect(unbatched.entities == 12 && unbatched.draws == 12 && unbatched.batches == 12 && unbatched.culled == 0,
           "unbatched: twelve draws, each its own batch" + stats_text(unbatched));

    renderer.set_visibility({false, true});
    const lykoware::Image batched = draw(device, renderer, entities, "batched");
    const render::DrawStats grouped = renderer.last_draw_stats();
    expect(grouped.entities == 12 && grouped.draws == 12 && grouped.batches == 4,
           "batched: twelve draws in four batches, one per mesh and texture" + stats_text(grouped));
    expect(same(plain, batched), "batching does not change the image");
    const lykoware::Image again = draw(device, renderer, entities, "batched again");
    expect(same(batched, again) && renderer.last_draw_stats().batches == 4, "the batched order is the same on every draw");

    renderer.set_visibility({});
    const lykoware::Image off = draw(device, renderer, entities, "batching off again");
    expect(renderer.last_draw_stats().batches == 12 && same(plain, off), "turning batching off restores ECS order");
}

void culling(rhi::Device& device, render::Renderer& renderer, const Setup& setup, const render::ShadowShaders& shadow)
{
    // Behind the camera; far to the left; centre just past the right edge (the view's half-width
    // at z = 0 is about 7.70 m) with its sphere reaching back in; behind the eye but scaled to hold it.
    const std::vector<Extra> extras = {
        {{0.0, 0.0, 14.0}}, {{-30.0, 0.0, 0.0}}, {{8.2, 0.0, 0.0}}, {{0.0, 0.0, 12.0}, {10.F, 10.F, 10.F}}};
    const ecs::World entities = scene(setup, extras);
    renderer.set_visibility({false, false});
    const lykoware::Image all = draw(device, renderer, entities, "unculled");
    const render::DrawStats unculled = renderer.last_draw_stats();
    expect(unculled.entities == 16 && unculled.culled == 0, "without culling every entity is drawn" + stats_text(unculled));

    renderer.set_visibility({true, false});
    const lykoware::Image culled = draw(device, renderer, entities, "culled");
    const render::DrawStats kept = renderer.last_draw_stats();
    expect(kept.culled == 2 && kept.entities == 14 && renderer.last_draw_count() == 14,
           "the cube behind the camera and the one far left are culled, and only they" + stats_text(kept));
    expect(same(all, culled), "culling does not change the image (the straddling cube survives)");

    renderer.set_visibility({true, true});
    const lykoware::Image both = draw(device, renderer, entities, "culled and batched");
    expect(same(all, both) && renderer.last_draw_stats().culled == 2, "culling and batching together keep the image");

    // A culled cube still casts its shadow: every entity is a caster.
    rhi::RhiError error;
    if (renderer.enable_shadows(shadow, {}, error)) {
        static_cast<void>(draw(device, renderer, entities, "culled with shadows"));
        expect(renderer.last_draw_stats().culled == 2 && renderer.last_shadow_caster_count() == 16,
               "culled entities still cast shadows (" + std::to_string(renderer.last_shadow_caster_count()) + " casters)");
        renderer.disable_shadows();
    } else {
        expect(false, "shadows enable: " + error.message);
    }
    renderer.set_visibility({});
}

}

int main(int argc, char* argv[])
{
    if (argc != 6) {
        std::cerr << "usage: lykoware_render_visibility_test LIT.vert LIT.frag SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.visibility test";
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
    const std::vector<std::uint32_t> lit_vertex = words(argv[1]);
    const std::vector<std::uint32_t> lit_fragment = words(argv[2]);
    const std::vector<std::uint32_t> caster = words(argv[3]);
    const std::vector<std::uint32_t> receiver_vertex = words(argv[4]);
    const std::vector<std::uint32_t> receiver_fragment = words(argv[5]);
    if (g_failures > 0) {
        return EXIT_FAILURE;
    }
    const render::ShadowShaders shadow{caster, receiver_vertex, receiver_fragment};

    const std::size_t buffers_before = rhi::live_buffer_count();
    const std::size_t textures_before = rhi::live_texture_count();
    {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        const std::size_t buffers_base = rhi::live_buffer_count();
        const std::size_t textures_base = rhi::live_texture_count();
        expect(buffers_base > buffers_before && textures_base > textures_before,
               "the renderer's own cube and fallback texture are counted as live");
        const render::GpuResources empty = renderer->gpu_resources();
        expect(empty.meshes == 0 && empty.textures == 0 && empty.mesh_bytes == 0, "a new renderer holds no added resources");

        const auto box_mesh = renderer->add_mesh(box(), error);
        const auto red = solid(*renderer, 220, 40, 40);
        const auto green = solid(*renderer, 40, 200, 60);
        if (!box_mesh || !red || !green) {
            std::cerr << "FAIL: the box and textures upload: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        const Setup setup{*box_mesh, {*red, *green}};
        batching(*device, *renderer, setup);
        culling(*device, *renderer, setup, shadow);

        // A second mesh whose part names the red texture, then the books.
        const render::MeshPart tinted{0, 36, {1.F, 1.F, 1.F}, *red};
        const auto tinted_box = renderer->add_mesh(box(), std::span(&tinted, 1), error);
        expect(tinted_box.has_value(), "a mesh with a textured part uploads");
        const render::GpuResources held = renderer->gpu_resources();
        expect(held.meshes == 2 && held.textures == 2 && held.mesh_bytes > 0, "two added meshes and two textures are held");
        expect(rhi::live_buffer_count() == buffers_base + 4 && rhi::live_texture_count() == textures_base + 2,
               "each added mesh holds two buffers and each texture one");

        expect(!renderer->remove_texture(*red, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "a texture a live mesh part names is not freed");
        expect(!renderer->remove_mesh(render::MeshId{99, box_mesh->owner}, error)
                   && error.failure == rhi::RhiFailure::InvalidArgument,
               "an id this renderer never made is refused");
        expect(!renderer->remove_mesh(render::MeshId{}, error) && !renderer->remove_texture(render::TextureId{}, error),
               "the empty ids are refused");
        expect(renderer->remove_mesh(*tinted_box, error), "the tinted mesh is freed: " + error.message);
        expect(!renderer->remove_mesh(*tinted_box, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "a mesh is not freed twice");
        expect(renderer->remove_texture(*red, error), "the red texture is freed once no part names it: " + error.message);
        expect(!renderer->remove_texture(*red, error), "a texture is not freed twice");
        expect(!renderer->add_mesh(box(), std::span(&tinted, 1), error), "a part naming a freed texture is refused");

        // Freed ids draw nothing and do not break the frame: of the twelve, only the built-in cubes
        // with the live green texture remain (entities 2, 6 and 10).
        expect(renderer->remove_mesh(*box_mesh, error), "the box is freed: " + error.message);
        const ecs::World stale = scene(setup, {});
        static_cast<void>(draw(*device, *renderer, stale, "stale ids"));
        expect(renderer->last_draw_count() == 3, "entities with a freed mesh or texture are not drawn ("
                                                      + std::to_string(renderer->last_draw_count()) + " drawn)");

        expect(renderer->remove_texture(*green, error), "the green texture is freed: " + error.message);
        const render::GpuResources none = renderer->gpu_resources();
        expect(none.meshes == 0 && none.textures == 0 && none.mesh_bytes == 0, "nothing added is held after freeing");
        expect(rhi::live_buffer_count() == buffers_base && rhi::live_texture_count() == textures_base,
               "freeing returns the live counts to the renderer's own");

        const auto fresh = renderer->add_mesh(box(), error);
        expect(fresh && fresh->value == 3, "a new mesh gets a new id, not a freed one");
    }
    expect(rhi::live_buffer_count() == buffers_before && rhi::live_texture_count() == textures_before,
           "destroying the renderer frees every buffer and texture it made");

    // Deliberate break: a buffer held past the check is caught as a leak.
    {
        const std::array<std::byte, 16> bytes{};
        auto leaked = rhi::Buffer::create(*device, rhi::BufferUsage::Vertex, bytes, error);
        expect(leaked && rhi::live_buffer_count() == buffers_before + 1, "a buffer still held shows up as live (a leak)");
    }
    expect(rhi::live_buffer_count() == buffers_before, "and is gone once freed");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.visibility: culling, batching and GPU resources pass\n";
    return EXIT_SUCCESS;
}
