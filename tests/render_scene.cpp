// render.scene: the falling-crates milestone (ADR 0026). Physics moves ECS entities
// (physics_ecs::Link); the renderer draws every entity with a Transform and a MeshRenderer,
// camera-relative, into an offscreen target. Pixels are sampled well inside each object, so
// different GPUs' edge rasterisation does not matter.
//
// Usage: lykoware_render_scene_test LIT_VERTEX.spv LIT_FRAGMENT.spv OUTPUT_DIR
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device).

#include <lykoware/clock.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/physics.hpp>
#include <lykoware/physics_ecs.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace physics = lykoware::physics;
namespace render = lykoware::render;
namespace rhi = lykoware::rhi;
namespace ecs = lykoware::ecs;
namespace link = lykoware::physics_ecs;

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

struct Pixel {
    int r;
    int g;
    int b;
};

Pixel at(const lykoware::Image& image, std::pair<std::uint32_t, std::uint32_t> xy)
{
    const auto pixels = image.pixels();
    const std::size_t offset = (static_cast<std::size_t>(xy.second) * image.width() + xy.first) * 3;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2]};
}

// The pixel a world point lands on, with the renderer's camera maths.
std::pair<std::uint32_t, std::uint32_t> pixel_of(const render::Camera& camera, math::DVec3 point)
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

const rhi::ClearColor sky{0.45F, 0.6F, 0.8F, 1.F};

struct Crate {
    double x;
    double drop_height;
    math::Vec3 colour;
};
constexpr Crate crates[3] = {
    {-2.0, 3.0, {0.9F, 0.1F, 0.1F}},
    {0.0, 4.0, {0.1F, 0.8F, 0.1F}},
    {2.0, 5.0, {0.1F, 0.2F, 0.9F}},
};

struct Result {
    lykoware::Image image;
    std::vector<math::DVec3> crate_positions;
    std::size_t drawn = 0;
};

// Drops the crates onto a grey ground, steps 120 ticks and draws the scene. Everything is
// offset by `offset` (camera included).
Result run(rhi::Device& device, render::Renderer& renderer, math::DVec3 offset)
{
    Result result;
    std::string error;
    physics::WorldConfig config;
    config.worker_threads = 1;
    auto world = physics::World::create(config, error);
    ecs::World entities;
    link::Link linked(entities, *world);

    physics::BodyDesc ground;
    ground.shape = physics::BoxShape{{10.F, 0.5F, 10.F}};
    ground.motion = physics::MotionType::Static;
    ground.position = offset + math::Vec3{0.F, -0.5F, 0.F};
    const ecs::Entity ground_entity = entities.create();
    linked.add_body(ground_entity, ground, error);
    entities.add<render::MeshRenderer>(ground_entity, render::MeshRenderer{render::MeshShape::Cube, {20.F, 1.F, 20.F},
                                                                          {0.5F, 0.5F, 0.5F}});
    std::vector<ecs::Entity> crate_entities;
    for (const Crate& crate : crates) {
        physics::BodyDesc box;
        box.shape = physics::BoxShape{{0.5F, 0.5F, 0.5F}};
        box.position = offset + math::Vec3{static_cast<float>(crate.x), static_cast<float>(crate.drop_height), 0.F};
        const ecs::Entity e = entities.create();
        linked.add_body(e, box, error);
        entities.add<render::MeshRenderer>(e, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, crate.colour});
        crate_entities.push_back(e);
    }
    // A wall behind the crates, created after them so it is drawn after them: the crates show
    // in front of it only if the depth test works. It has no physics, only a Transform.
    const ecs::Entity wall = entities.create();
    entities.add<lykoware::Transform>(wall, lykoware::Transform{offset + math::Vec3{0.F, 3.F, -4.F}, {}});
    entities.add<render::MeshRenderer>(wall, render::MeshRenderer{render::MeshShape::Cube, {14.F, 6.F, 0.5F},
                                                                  {0.35F, 0.25F, 0.2F}});

    std::string clock_error;
    const auto clock = lykoware::FixedStep::create({}, clock_error);
    const float tick = clock ? static_cast<float>(clock->tick_seconds()) : 1.F / 60.F;
    for (int i = 0; i < 120; ++i) {
        linked.step(tick);
    }
    for (const ecs::Entity e : crate_entities) {
        result.crate_positions.push_back(entities.get<lykoware::Transform>(e)->position);
    }

    const auto before_draw = world->state_hash();
    rhi::RhiError rhi_error;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, rhi_error);
    const render::Camera camera{offset + math::Vec3{0.F, 3.F, 9.F}, offset + math::Vec3{0.F, 0.5F, 0.F}};
    if (!target || !renderer.draw(entities, camera, *target, sky, rhi_error)) {
        std::cerr << "  draw: " << rhi_error.message << '\n';
        return result;
    }
    result.drawn = renderer.last_draw_count();
    expect(world->state_hash() == before_draw, "GPU draw leaves physics state unchanged");
    if (!rhi::RenderTargetCapture{*target}.capture(result.image)) {
        result.image.clear();
    }
    return result;
}

void check_scene(const Result& result, math::DVec3 offset)
{
    expect(result.image.width() == width && result.image.height() == height, "the scene renders");
    if (result.image.width() != width) {
        return;
    }
    expect(result.drawn == 5, "the ground, three crates and the wall are drawn");
    const render::Camera camera{offset + math::Vec3{0.F, 3.F, 9.F}, offset + math::Vec3{0.F, 0.5F, 0.F}};
    for (std::size_t i = 0; i < 3; ++i) {
        const math::DVec3 p = result.crate_positions[i];
        expect(std::fabs(p.y - offset.y - 0.5) < 0.05, "crate " + std::to_string(i) + " has landed on the ground");
        // The middle of the crate's front face (+Z, towards the camera).
        const Pixel pixel = at(result.image, pixel_of(camera, p + math::Vec3{0.F, 0.F, 0.5F}));
        const math::Vec3 c = crates[i].colour;
        const bool red = c.x > 0.5F;
        const bool green = c.y > 0.5F;
        const bool dominant = red ? pixel.r > pixel.g + 40 && pixel.r > pixel.b + 40
                            : green ? pixel.g > pixel.r + 40 && pixel.g > pixel.b + 40
                                    : pixel.b > pixel.r + 40 && pixel.b > pixel.g + 40;
        expect(dominant, "crate " + std::to_string(i) + " shows in its own colour where it landed");
    }
    const Pixel ground = at(result.image, pixel_of(camera, offset + math::Vec3{0.F, 0.F, 3.F}));
    expect(std::abs(ground.r - ground.g) < 4 && std::abs(ground.g - ground.b) < 4 && ground.r > 60,
           "the ground in front of the crates is lit grey");
    const Pixel corner = at(result.image, {2, 2});
    expect(corner.r == 115 && corner.g == 153 && corner.b == 204, "the sky keeps the clear colour");
    const Pixel wall = at(result.image, pixel_of(camera, offset + math::Vec3{-5.F, 4.F, -3.75F}));
    // Lit front face: 0.35 * 0.451 * 255 = 40 red.
    expect(wall.r > wall.g && wall.g > wall.b && wall.r > 30, "the wall shows behind the crates");
}

}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "usage: lykoware_render_scene_test LIT_VERTEX.spv LIT_FRAGMENT.spv OUTPUT_DIR\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path output = argv[3];
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.scene test";
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

    Result first;
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

        first = run(*device, *renderer, {});
        check_scene(first, {});
        const Result again = run(*device, *renderer, {});
        expect(again.image.width() == width && lykoware::compare_images(first.image, again.image, 0).matches,
               "the same scene gives identical bytes every run");

        const math::DVec3 far{16000.0, 0.0, -16000.0};
        const Result distant = run(*device, *renderer, far);
        check_scene(distant, far);
        expect(distant.image.width() == width && lykoware::compare_images(first.image, distant.image, 0).matches,
               "the scene 16 km from the origin gives exactly the same image");

        ecs::World empty;
        auto flat = rhi::RenderTarget::create(*device, {width, height, false}, error);
        expect(flat && !renderer->draw(empty, {}, *flat, sky, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "drawing into a target without depth is refused");
    }

    // The frame is always written under the build directory, for a person to look at.
    std::error_code ignored;
    std::filesystem::create_directories(output, ignored);
    if (first.image.width() != 0 && lykoware::write_ppm(first.image, output / "scene.ppm")) {
        std::cout << "wrote " << (output / "scene.ppm").string() << '\n';
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " scene assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "render scene passed\n";
    return EXIT_SUCCESS;
}
