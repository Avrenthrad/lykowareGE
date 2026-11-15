// render.warmup (ADR 0100, 06.10): no pipeline compiles inside the measured frames. With
// post-processing on, enabling shadows and transparency leaves the float-target pipelines stale.
// Without a warm-up the first draw compiles them (the hitch, and this test's deliberate break);
// after Renderer::warm_up, rhi::pipeline_compile_count() must not move across 60 frames of a
// changing scene, and the image must be the one the unwarmed renderer drew, byte for byte.
//
// Usage: lykoware_render_warmup_test LIT.vert LIT.frag POST.vert POST.frag TRANSPARENT.vert
//        TRANSPARENT.frag SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag
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
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace render = lykoware::render;
namespace rhi = lykoware::rhi;
namespace ecs = lykoware::ecs;

constexpr int exit_skip = 77;
constexpr std::uint32_t width = 96;
constexpr std::uint32_t height = 64;
constexpr int measured_frames = 60;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

const rhi::ClearColor clear{0.F, 0.F, 0.F, 1.F};

// A floor, an opaque cube and a transparent one, so the lit, shadowed and blended pipelines all draw.
ecs::World scene(double slide)
{
    ecs::World entities;
    const ecs::Entity floor = entities.create();
    entities.add<lykoware::Transform>(floor, lykoware::Transform{{0.0, -1.0, 0.0}, {}});
    entities.add<render::MeshRenderer>(floor, render::MeshRenderer{render::MeshShape::Cube, {12.F, 0.2F, 12.F}, {0.6F, 0.6F, 0.6F}});
    const ecs::Entity cube = entities.create();
    entities.add<lykoware::Transform>(cube, lykoware::Transform{{slide, 0.0, 0.0}, {}});
    entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.9F, 0.2F, 0.2F}});
    const ecs::Entity glass = entities.create();
    entities.add<lykoware::Transform>(glass, lykoware::Transform{{-slide, 0.0, 2.0}, {}});
    render::MeshRenderer pane{render::MeshShape::Cube, {1.F, 1.F, 0.1F}, {0.2F, 0.4F, 0.9F}};
    pane.opacity = 0.5F;
    entities.add<render::MeshRenderer>(glass, pane);
    return entities;
}

bool same(const lykoware::Image& a, const lykoware::Image& b)
{
    return !a.pixels().empty() && a.pixels().size() == b.pixels().size()
        && std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin());
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

// A renderer with post-processing first, then shadows and transparency: the order that leaves
// the float-target pipelines stale.
std::unique_ptr<render::Renderer> stale_renderer(rhi::Device& device, const std::vector<std::vector<std::uint32_t>>& code)
{
    rhi::RhiError error;
    auto renderer = render::Renderer::create(device, rhi::ColorFormat::Rgba8Unorm, code[0], code[1], error);
    const bool ok = renderer && renderer->enable_post({code[2], code[3]}, {}, error)
        && renderer->enable_transparency({code[4], code[5]}, error)
        && renderer->enable_shadows({code[6], code[7], code[8]}, {}, error);
    expect(ok, "the renderer and its features are made: " + error.message);
    return ok ? std::move(renderer) : nullptr;
}

}


int main(int argc, char* argv[])
{
    if (argc != 10) {
        std::cerr << "usage: lykoware_render_warmup_test LIT.vert LIT.frag POST.vert POST.frag TRANSPARENT.vert "
                     "TRANSPARENT.frag SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.warmup test";
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
    std::vector<std::vector<std::uint32_t>> code;
    for (int i = 1; i < argc; ++i) {
        code.push_back(words(argv[i]));
    }
    if (g_failures > 0) {
        return EXIT_FAILURE;
    }
    auto target = rhi::RenderTarget::create(*device, {width, height, true}, error);
    if (!target) {
        std::cerr << "FAIL: the target is created: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    const render::Camera camera{{4.0, 3.0, 8.0}, {0.0, 0.0, 0.0}};
    lykoware::Image unwarmed;
    {
        // Deliberate break: no warm-up, so the first measured frame compiles pipelines.
        auto renderer = stale_renderer(*device, code);
        if (!renderer) {
            return EXIT_FAILURE;
        }
        expect(renderer->needs_warm_up(), "features enabled after post-processing leave pipelines to compile");
        const std::uint64_t before = rhi::pipeline_compile_count();
        expect(renderer->draw(scene(0.0), camera, *target, clear, error), "the unwarmed frame draws: " + error.message);
        const std::uint64_t first = rhi::pipeline_compile_count() - before;
        expect(first > 0, "without a warm-up the first frame compiles pipelines (the check below would fail)");
        expect(!renderer->needs_warm_up(), "after that frame nothing is left to compile");
        expect(target->read_back(unwarmed, error), "the unwarmed image is read: " + error.message);
        const std::uint64_t settled = rhi::pipeline_compile_count();
        expect(renderer->draw(scene(0.5), camera, *target, clear, error) && rhi::pipeline_compile_count() == settled,
               "and the second frame compiles none");
    }
    {
        auto renderer = stale_renderer(*device, code);
        if (!renderer) {
            return EXIT_FAILURE;
        }
        const std::uint64_t cold = rhi::pipeline_compile_count();
        expect(renderer->warm_up(error) && !renderer->needs_warm_up(), "the warm-up succeeds: " + error.message);
        const std::uint64_t warmed = rhi::pipeline_compile_count();
        expect(warmed > cold, "the warm-up is where the pipelines compile");
        expect(renderer->warm_up(error) && rhi::pipeline_compile_count() == warmed, "a second warm-up compiles nothing");

        // The measured window: a moving scene and camera, features in use, and not one compilation.
        lykoware::Image first;
        bool drew = true;
        for (int frame = 0; frame < measured_frames && drew; ++frame) {
            const double t = static_cast<double>(frame) / measured_frames;
            const render::Camera moving{{4.0 - 8.0 * t, 3.0, 8.0}, {0.0, 0.0, 0.0}};
            drew = renderer->draw(scene(frame == 0 ? 0.0 : t * 2.0), frame == 0 ? camera : moving, *target, clear, error);
            if (frame == 0 && drew) {
                drew = target->read_back(first, error);
            }
        }
        expect(drew, "the measured frames draw: " + error.message);
        const std::uint64_t compiled = rhi::pipeline_compile_count() - warmed;
        expect(compiled == 0, "no pipeline compiles during the measured frames (" + std::to_string(compiled) + " did)");
        expect(renderer->last_shadow_caster_count() > 0 && renderer->last_transparent_draw_count() == 1,
               "the frames used the lit, shadowed and blended paths");
        expect(same(first, unwarmed), "a warmed renderer draws the same image, byte for byte");

        // A feature enabled later is stale again until the next warm-up.
        renderer->disable_transparency();
        expect(!renderer->needs_warm_up(), "disabling a feature compiles nothing");
        expect(renderer->enable_transparency({code[4], code[5]}, error) && renderer->needs_warm_up(),
               "enabling a feature again leaves work for the warm-up");
        expect(renderer->warm_up(error) && !renderer->needs_warm_up(), "which the warm-up does: " + error.message);
        const std::uint64_t again = rhi::pipeline_compile_count();
        expect(renderer->draw(scene(1.0), camera, *target, clear, error) && rhi::pipeline_compile_count() == again,
               "and the next frame compiles none");
    }
    {
        // Without post-processing nothing is built lazily: the warm-up has nothing to do.
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, code[0], code[1], error);
        const bool made = renderer && renderer->enable_transparency({code[4], code[5]}, error);
        expect(made, "a renderer without post-processing is made: " + error.message);
        if (made) {
            const std::uint64_t before = rhi::pipeline_compile_count();
            expect(!renderer->needs_warm_up() && renderer->warm_up(error) && rhi::pipeline_compile_count() == before,
                   "the warm-up compiles nothing");
            expect(renderer->draw(scene(0.0), camera, *target, clear, error) && rhi::pipeline_compile_count() == before,
                   "and neither does the first frame");
        }
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.warmup: no pipeline compiles in " << measured_frames << " measured frames after the warm-up\n";
    return EXIT_SUCCESS;
}
