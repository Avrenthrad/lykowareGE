// render.fog (ADR 0099): distance fog in the post pass. A wall at a known distance must match
// mix(wall, fog, min(max_opacity, 1 - exp(-density * max(d - start, 0)))), tone-mapped, within 2
// of 255 at every sampled pixel, where d is the distance from the eye to the wall along that
// pixel's own view direction: level, and tilted far from the origin. Pixels with nothing drawn
// keep the sky or the clear colour, and with fog off the image is back, byte for byte.
//
// Usage: lykoware_render_fog_test LIT.vert LIT.frag FOG.vert FOG.frag POST.vert POST.frag SKY.vert SKY.frag
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
const math::Vec3 wall_colour{0.9F, 0.3F, 0.1F};

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

Rgb bytes(math::Vec3 colour)
{
    return {static_cast<int>(std::lround(std::min(colour.x, 1.F) * 255.F)), static_cast<int>(std::lround(std::min(colour.y, 1.F) * 255.F)),
            static_cast<int>(std::lround(std::min(colour.z, 1.F) * 255.F))};
}

math::Vec3 reinhard(math::Vec3 c)
{
    return {c.x / (1.F + c.x), c.y / (1.F + c.y), c.z / (1.F + c.z)};
}

// The stated formula: the wall's colour seen through `distance` metres of fog.
math::Vec3 fogged(const render::Fog& fog, float distance)
{
    const float share = std::min(fog.max_opacity, 1.F - std::exp(-fog.density * std::max(distance - fog.start, 0.F)));
    return wall_colour + (fog.colour - wall_colour) * share;
}

// A wall 400 m square whose near face is the plane z = wall_z, centred on (x, 0).
ecs::World with_wall(double x, double wall_z)
{
    ecs::World entities;
    const ecs::Entity wall = entities.create();
    entities.add<lykoware::Transform>(wall, lykoware::Transform{{x, 0.0, wall_z - 0.5}, {}});
    entities.add<render::MeshRenderer>(wall, render::MeshRenderer{render::MeshShape::Cube, {400.F, 400.F, 1.F}, wall_colour});
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

// Every 8th pixel against the formula for a wall on the plane z = wall_z; the worst channel
// difference. The distance is along each pixel's own view direction from the camera's eye.
int worst_fog(const lykoware::Image& image, const render::Camera& camera, const render::Fog& fog, double wall_z)
{
    int worst = 0;
    for (std::uint32_t y = 4; y < height; y += 8) {
        for (std::uint32_t x = 4; x < width; x += 8) {
            const math::Vec3 d = direction(camera, x, y);
            const float distance = static_cast<float>(wall_z - camera.eye.z) / d.z;
            worst = std::max(worst, furthest(pixel(image, x, y), bytes(reinhard(fogged(fog, distance)))));
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
    if (argc != 9) {
        std::cerr << "usage: lykoware_render_fog_test LIT.vert LIT.frag FOG.vert FOG.frag POST.vert POST.frag SKY.vert SKY.frag\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.fog test";
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
    const render::FogShaders fog_shaders{spirv[2], spirv[3]};
    const render::PostShaders post_shaders{spirv[4], spirv[5]};
    {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, spirv[0], spirv[1], error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        // Ambient 1: the wall's linear colour is its base colour exactly, whatever it faces.
        render::Lighting flat = renderer->lighting();
        flat.ambient = 1.F;
        expect(renderer->set_lighting(flat, error), "flat lighting is set: " + error.message);

        const ecs::World nothing;
        const double wall_z = -20.0;
        const ecs::World wall = with_wall(0.0, wall_z);
        const std::uint32_t cx = width / 2;
        const std::uint32_t cy = height / 2;
        const lykoware::Image plain = draw(*device, *renderer, wall, level, "the wall without post");

        render::Fog fog;
        fog.colour = {0.2F, 0.5F, 0.9F};
        fog.density = 0.05F;
        expect(!renderer->enable_fog(fog_shaders, fog, error) && error.failure == rhi::RhiFailure::InvalidArgument
                   && !renderer->fog(),
               "fog without post-processing is refused");
        expect(renderer->enable_post(post_shaders, {}, error), "post-processing enables: " + error.message);
        const lykoware::Image mapped = draw(*device, *renderer, wall, level, "the wall with post");
        const lykoware::Image empty_mapped = draw(*device, *renderer, nothing, level, "nothing with post");
        expect(furthest(pixel(mapped, cx, cy), bytes(reinhard(wall_colour))) <= 2,
               "without fog the wall is its tone-mapped colour: " + text(pixel(mapped, cx, cy)));

        expect(!renderer->enable_fog({}, fog, error) && !renderer->fog(), "missing fog shaders are refused");
        expect(renderer->enable_fog(fog_shaders, fog, error) && renderer->fog().has_value(), "fog enables: " + error.message);

        // A wall 20 m ahead: 63% fog at the centre, more towards the corners, where the wall is further.
        const lykoware::Image foggy = draw(*device, *renderer, wall, level, "the wall in fog");
        const int worst = worst_fog(foggy, level, fog, wall_z);
        expect(worst <= 2, "every sampled pixel matches the fog for its distance (worst " + std::to_string(worst) + ")");
        const Rgb centre = pixel(foggy, cx, cy);
        const Rgb corner = pixel(foggy, 2, 2);
        expect(centre[2] > pixel(mapped, cx, cy)[2] + 40 && corner[2] > centre[2] + 2 && corner[0] < centre[0] - 2,
               "the fog shows, and the corner, further away, has more: " + text(centre) + " vs " + text(corner));
        expect(renderer->last_draw_count() == 1 && renderer->last_draw_stats().draws == 1, "fog is not counted as a draw");
        // Deliberate break: without the fog the same check is far off.
        expect(worst_fog(mapped, level, fog, wall_z) > 40, "without the fog pass the check fails");

        // A further wall has more fog; a nearer one less.
        const lykoware::Image far_wall = draw(*device, *renderer, with_wall(0.0, -60.0), level, "a wall at 60 m");
        const lykoware::Image near_wall = draw(*device, *renderer, with_wall(0.0, -5.0), level, "a wall at 5 m");
        expect(worst_fog(far_wall, level, fog, -60.0) <= 2 && worst_fog(near_wall, level, fog, -5.0) <= 2,
               "walls at 5 m and 60 m match their own distances");
        expect(pixel(near_wall, cx, cy)[0] > centre[0] + 20 && pixel(far_wall, cx, cy)[0] < centre[0] - 20,
               "nearer is clearer and further is foggier");

        // Tilted, turned and far from the origin: the distance is from the eye, not from the screen plane.
        const render::Camera tilted{{1000.0, 2.0, 5.0}, {1004.0, 4.0, -20.0}};
        const int worst_tilted = worst_fog(draw(*device, *renderer, with_wall(1000.0, wall_z), tilted, "a tilted view"), tilted, fog, wall_z);
        expect(worst_tilted <= 2, "a tilted view far from the origin matches too (worst " + std::to_string(worst_tilted) + ")");

        // start: no fog before it, and the formula past it.
        render::Fog late = fog;
        late.start = 15.F;
        expect(renderer->enable_fog(fog_shaders, late, error), "fog with a start enables: " + error.message);
        const int worst_late = worst_fog(draw(*device, *renderer, wall, level, "fog from 15 m"), level, late, wall_z);
        expect(worst_late <= 2, "fog starting at 15 m matches (worst " + std::to_string(worst_late) + ")");
        late.start = 100.F;
        expect(renderer->enable_fog(fog_shaders, late, error), "fog with a far start enables: " + error.message);
        expect(same(draw(*device, *renderer, wall, level, "fog from 100 m"), mapped), "a wall nearer than start is untouched, byte for byte");

        // max_opacity caps the fog; density 0 is none.
        render::Fog capped = fog;
        capped.density = 10.F;
        capped.max_opacity = 0.5F;
        expect(renderer->enable_fog(fog_shaders, capped, error), "capped fog enables: " + error.message);
        const Rgb half = pixel(draw(*device, *renderer, wall, level, "capped fog"), cx, cy);
        expect(furthest(half, bytes(reinhard(wall_colour + (fog.colour - wall_colour) * 0.5F))) <= 2,
               "thick fog capped at 0.5 is an even mix: " + text(half));
        render::Fog none = fog;
        none.density = 0.F;
        expect(renderer->enable_fog(fog_shaders, none, error), "fog of density 0 enables: " + error.message);
        expect(same(draw(*device, *renderer, wall, level, "density 0"), mapped), "density 0 changes nothing");

        // Where nothing is drawn there is no distance: the clear colour and the sky stay.
        expect(renderer->enable_fog(fog_shaders, fog, error), "fog returns: " + error.message);
        expect(same(draw(*device, *renderer, nothing, level, "fog over nothing"), empty_mapped), "the clear colour is not fogged");
        if (renderer->enable_sky({spirv[6], spirv[7]}, {}, error)) {
            const lykoware::Image sky_fog = draw(*device, *renderer, nothing, level, "fog over the sky");
            renderer->disable_fog();
            expect(same(sky_fog, draw(*device, *renderer, nothing, level, "the sky alone")), "the sky is not fogged");
            renderer->disable_sky();
            expect(renderer->enable_fog(fog_shaders, fog, error), "fog returns after the sky: " + error.message);
        } else {
            expect(false, "the sky enables: " + error.message);
        }

        // Refusals leave the fog as it was.
        const auto refused = [&](const render::Fog& bad) {
            return !renderer->enable_fog(fog_shaders, bad, error) && error.failure == rhi::RhiFailure::InvalidArgument
                && renderer->fog() && renderer->fog()->density == fog.density;
        };
        render::Fog bad = fog;
        bad.colour.y = std::numeric_limits<float>::quiet_NaN();
        expect(refused(bad), "a NaN colour is refused");
        bad = fog;
        bad.colour.x = -0.1F;
        expect(refused(bad), "a negative colour is refused");
        bad = fog;
        bad.density = -0.01F;
        expect(refused(bad), "a negative density is refused");
        bad = fog;
        bad.density = render::fog_max_density * 2.F;
        expect(refused(bad), "a density past the limit is refused");
        bad = fog;
        bad.start = -1.F;
        expect(refused(bad), "a negative start is refused");
        bad = fog;
        bad.start = std::numeric_limits<float>::infinity();
        expect(refused(bad), "an infinite start is refused");
        bad = fog;
        bad.max_opacity = 1.5F;
        expect(refused(bad), "a max_opacity above 1 is refused");
        expect(same(draw(*device, *renderer, wall, level, "after the refusals"), foggy), "and the image is the earlier fog");

        // Off again: the tone map alone, then the first image.
        renderer->disable_fog();
        expect(!renderer->fog() && same(draw(*device, *renderer, wall, level, "fog disabled"), mapped),
               "disabling fog draws exactly as post-processing alone");
        expect(renderer->enable_fog(fog_shaders, fog, error), "fog enables again: " + error.message);
        renderer->disable_post();
        expect(!renderer->fog() && !renderer->post(), "disabling post-processing turns fog off with it");
        expect(same(draw(*device, *renderer, wall, level, "post disabled"), plain), "and draws exactly as at the start");
    }
    {
        // The RHI rule under it: a pipeline that reads the scene's depth needs the texture set.
        const auto vertex = rhi::Shader::load(*device, spirv[2], rhi::ShaderStage::Vertex, error);
        const auto fragment = vertex ? rhi::Shader::load(*device, spirv[3], rhi::ShaderStage::Fragment, error) : nullptr;
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
        desc.push_constant_bytes = 48;
        desc.scene_depth = true;
        expect(vertex && fragment && !rhi::GraphicsPipeline::create(*device, desc, error)
                   && error.failure == rhi::RhiFailure::InvalidArgument,
               "a scene-depth pipeline without the texture set is refused");
        desc.texture = true;
        const auto pipeline = rhi::GraphicsPipeline::create(*device, desc, error);
        expect(pipeline && pipeline->scene_depth(), "and with it is made: " + error.message);
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.fog: fog by distance, start, the cap, the untouched sky and the refusals pass\n";
    return EXIT_SUCCESS;
}
