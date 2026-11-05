// render.texture: the renderer drawing a textured mesh from the ECS (ADR 0037). A quad facing
// the camera shows a 2x1 red|blue texture lit by the engine's lit shader; pixels are sampled
// well inside each half.
//
// Usage: lykoware_render_texture_test LIT_VERTEX.spv LIT_FRAGMENT.spv OUTPUT_DIR
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device).

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/image_decode.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
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

// A 2 m square in the XY plane facing +Z, with texture coordinates (0, 0) at its top-left.
lykoware::MeshData quad()
{
    lykoware::MeshData mesh;
    mesh.positions = {{-1.F, -1.F, 0.F}, {1.F, -1.F, 0.F}, {1.F, 1.F, 0.F}, {-1.F, 1.F, 0.F}};
    mesh.normals = std::vector<math::Vec3>(4, {0.F, 0.F, 1.F});
    mesh.uvs = {{0.F, 1.F}, {1.F, 1.F}, {1.F, 0.F}, {0.F, 0.F}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "usage: lykoware_render_texture_test LIT_VERTEX.spv LIT_FRAGMENT.spv OUTPUT_DIR\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.texture test";
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
    lykoware::Image image;
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
        const auto mesh = renderer->add_mesh(quad(), error);
        lykoware::RgbaImage red_blue;
        red_blue.width = 2;
        red_blue.height = 1;
        red_blue.pixels = {255, 0, 0, 255, 0, 0, 255, 255};
        render::TextureSettings nearest;
        nearest.filter = rhi::TextureFilter::Nearest;
        nearest.wrap = rhi::TextureWrap::ClampToEdge;
        nearest.mipmaps = false;
        const auto texture = renderer->add_texture(red_blue, nearest, error);
        expect(mesh && texture, "the quad and its texture are uploaded");
        expect(!renderer->add_texture(lykoware::RgbaImage{}, {}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "an empty image is refused");
        if (!mesh || !texture) {
            return EXIT_FAILURE;
        }

        ecs::World entities;
        const ecs::Entity textured = entities.create();
        entities.add<lykoware::Transform>(textured, lykoware::Transform{});
        entities.add<render::MeshRenderer>(
            textured, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F}, *mesh, *texture});
        const ecs::Entity plain = entities.create();
        entities.add<lykoware::Transform>(plain, lykoware::Transform{{2.5, 0.0, 0.0}, {}});
        entities.add<render::MeshRenderer>(plain, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.2F, 0.9F, 0.2F}});
        const ecs::Entity stranger = entities.create();
        entities.add<lykoware::Transform>(stranger, lykoware::Transform{{-2.5, 0.0, 0.0}, {}});
        entities.add<render::MeshRenderer>(
            stranger, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F}, {}, render::TextureId{42}});

        auto target = rhi::RenderTarget::create(*device, {width, height, true}, error);
        const render::Camera camera{{0.0, 0.0, 6.0}, {0.0, 0.0, 0.0}};
        if (!target || !renderer->draw(entities, camera, *target, sky, error) || !target->read_back(image, error)) {
            std::cerr << "FAIL: the scene draws: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        expect(renderer->last_draw_count() == 2, "the textured quad and the plain cube are drawn; a texture id from no renderer is not");

        // Facing +Z, the quad's light is 0.25 + 0.75 * (n . light); the texture is linear red and
        // blue, so each half stores that times 255 in its own channel.
        const math::Vec3 light = math::normalize(math::Vec3{0.4F, 1.F, 0.3F});
        const int lit = static_cast<int>(std::lround((0.25F + 0.75F * std::max(0.F, light.z)) * 255.F));
        const Pixel left = at(image, pixel_of(camera, {-0.5, 0.0, 0.0}));
        const Pixel right = at(image, pixel_of(camera, {0.5, 0.0, 0.0}));
        expect(std::abs(left.r - lit) <= 2 && left.g == 0 && left.b == 0,
               "the left half shows the red texel, lit (" + std::to_string(left.r) + " vs " + std::to_string(lit) + ")");
        expect(std::abs(right.b - lit) <= 2 && right.r == 0 && right.g == 0, "the right half shows the blue texel, lit");
        const Pixel cube = at(image, pixel_of(camera, {2.5, 0.0, 0.5}));
        expect(cube.g > cube.r + 40 && cube.g > cube.b + 40, "an untextured cube still shows its colour");
        const Pixel sky_pixel = at(image, pixel_of(camera, {-2.5, 0.0, 0.5}));
        expect(sky_pixel.r == 115 && sky_pixel.g == 153 && sky_pixel.b == 204, "where the unknown texture's cube would be, the sky shows");
    }
    if (instance->validation_error_count() != 0) {
        expect(false, std::to_string(instance->validation_error_count()) + " Vulkan validation errors");
    }
    std::error_code ignored;
    std::filesystem::create_directories(argv[3], ignored);
    if (image.width() != 0 && lykoware::write_ppm(image, std::filesystem::path(argv[3]) / "texture.ppm")) {
        std::cout << "wrote " << (std::filesystem::path(argv[3]) / "texture.ppm").string() << '\n';
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " render texture assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render texture passed\n";
    return EXIT_SUCCESS;
}
