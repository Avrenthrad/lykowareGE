// render.model: a glTF model loaded by Lykoware's reader, uploaded with Renderer::add_mesh and
// drawn from the ECS (ADR 0035). Pixels are sampled well inside the model.
//
// Usage: lykoware_render_model_test LIT_VERTEX.spv LIT_FRAGMENT.spv OUTPUT_DIR
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device).

#include <lykoware/ecs.hpp>
#include <lykoware/gltf.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include "support/png_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

std::string base64(const std::vector<std::byte>& data)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        std::uint32_t bits = static_cast<std::uint32_t>(data[i]) << 16U;
        bits |= i + 1 < data.size() ? static_cast<std::uint32_t>(data[i + 1]) << 8U : 0U;
        bits |= i + 2 < data.size() ? static_cast<std::uint32_t>(data[i + 2]) : 0U;
        out += alphabet[(bits >> 18U) & 63U];
        out += alphabet[(bits >> 12U) & 63U];
        out += i + 1 < data.size() ? alphabet[(bits >> 6U) & 63U] : '=';
        out += i + 2 < data.size() ? alphabet[bits & 63U] : '=';
    }
    return out;
}

// A 2 m wide ramp through the origin, rising away from the camera: the plane y + z = 0, facing
// up and towards +Z. Written as a .gltf with its data in a base64 URI and read by gltf::read.
std::optional<lykoware::MeshData> ramp(std::string& error)
{
    const float s = std::sqrt(0.5F);
    const float data[] = {
        -1.F, -0.5F, 0.5F, 1.F, -0.5F, 0.5F, 1.F, 0.5F, -0.5F, -1.F, 0.5F, -0.5F, // positions
        0.F,  s,     s,    0.F, s,     s,    0.F, s,    s,     0.F,  s,    s,     // normals
    };
    const std::uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    std::vector<std::byte> bin(sizeof(data) + sizeof(indices));
    std::memcpy(bin.data(), data, sizeof(data));
    std::memcpy(bin.data() + sizeof(data), indices, sizeof(indices));
    const std::string json = std::string{"{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"}
        + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],"
        + "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        + "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        + "{\"bufferView\":1,\"componentType\":5125,\"count\":6,\"type\":\"SCALAR\"}],"
        + "\"bufferViews\":[{\"buffer\":0,\"byteLength\":96},{\"buffer\":0,\"byteOffset\":96,\"byteLength\":24}],"
        + "\"buffers\":[{\"byteLength\":120,\"uri\":\"data:application/octet-stream;base64," + base64(bin) + "\"}]}";
    std::vector<std::byte> file(json.size());
    std::memcpy(file.data(), json.data(), json.size());
    return lykoware::gltf::read(file, {}, error);
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

struct Frame {
    lykoware::Image image;
    std::size_t drawn = 0;
};

// The ramp, stretched 3x upwards, at `offset`; a cube to its right; and an entity whose mesh id
// this renderer never made.
Frame draw(rhi::Device& device, render::Renderer& renderer, render::MeshId mesh, math::DVec3 offset)
{
    Frame frame;
    ecs::World entities;
    const ecs::Entity model = entities.create();
    entities.add<lykoware::Transform>(model, lykoware::Transform{offset, {}});
    entities.add<render::MeshRenderer>(model, render::MeshRenderer{render::MeshShape::Cube, {1.F, 3.F, 1.F}, {1.F, 1.F, 1.F}, mesh});
    const ecs::Entity cube = entities.create();
    entities.add<lykoware::Transform>(cube, lykoware::Transform{offset + math::Vec3{2.5F, 0.F, 0.F}, {}});
    entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.9F, 0.2F, 0.1F}});
    const ecs::Entity stranger = entities.create();
    entities.add<lykoware::Transform>(stranger, lykoware::Transform{offset + math::Vec3{-2.5F, 0.F, 0.F}, {}});
    entities.add<render::MeshRenderer>(stranger, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {0.F, 1.F, 0.F}, {99}});

    rhi::RhiError error;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const render::Camera camera{offset + math::Vec3{0.F, 1.F, 6.F}, offset};
    if (!target || !renderer.draw(entities, camera, *target, sky, error)) {
        std::cerr << "  draw: " << error.message << '\n';
        return frame;
    }
    frame.drawn = renderer.last_draw_count();
    if (!rhi::RenderTargetCapture{*target}.capture(frame.image)) {
        frame.image.clear();
    }
    return frame;
}

void check(const Frame& frame, math::DVec3 offset)
{
    expect(frame.image.width() == width, "the model renders");
    if (frame.image.width() != width) {
        return;
    }
    expect(frame.drawn == 2, "the model and the cube are drawn; a mesh id from no renderer is not");
    const render::Camera camera{offset + math::Vec3{0.F, 1.F, 6.F}, offset};
    // The ramp's normal (0, 1, 1) stretched 3x along Y becomes (0, 1/3, 1): the inverse
    // transpose. The lit shader gives colour * (0.25 + 0.75 * max(0, n . light)).
    const math::Vec3 normal = math::normalize(math::Vec3{0.F, 1.F / 3.F, 1.F});
    const math::Vec3 light = math::normalize(math::Vec3{0.4F, 1.F, 0.3F});
    const float shade = 0.25F + 0.75F * std::max(0.F, math::dot(normal, light));
    const int expected = static_cast<int>(std::lround(shade * 255.F));
    const Pixel middle = at(frame.image, pixel_of(camera, offset));
    expect(std::abs(middle.r - expected) <= 2 && middle.r == middle.g && middle.g == middle.b,
           "the stretched ramp is lit by its correctly transformed normal (" + std::to_string(middle.r) + " vs "
               + std::to_string(expected) + ")");
    const Pixel cube = at(frame.image, pixel_of(camera, offset + math::Vec3{2.5F, 0.F, 0.5F}));
    expect(cube.r > cube.g + 60 && cube.r > cube.b + 60, "the built-in cube still draws beside the model");
    const Pixel stranger = at(frame.image, pixel_of(camera, offset + math::Vec3{-2.5F, 0.F, 0.5F}));
    expect(stranger.r == 115 && stranger.g == 153 && stranger.b == 204, "where the unknown mesh would be, the sky shows");
}

}

// Two quads in one glTF mesh, as two primitives with their own materials: the left one textured
// red|blue (nearest, clamped, no mipmaps), the right one a plain green factor. Read with
// gltf::read_model and uploaded as the sandbox does: textures first, then the mesh as parts.
void test_materials(rhi::Device& device, render::Renderer& renderer, const std::filesystem::path& output)
{
    const float left[12] = {-2.2F, -1.F, 0.F, -0.2F, -1.F, 0.F, -0.2F, 1.F, 0.F, -2.2F, 1.F, 0.F};
    const float right[12] = {0.2F, -1.F, 0.F, 2.2F, -1.F, 0.F, 2.2F, 1.F, 0.F, 0.2F, 1.F, 0.F};
    const float normals[12] = {0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F};
    const float uvs[8] = {0.F, 1.F, 1.F, 1.F, 1.F, 0.F, 0.F, 0.F};
    const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    const std::vector<std::byte> png = lykoware::test::png_rgba(2, 1, {255, 0, 0, 255, 0, 0, 255, 255});
    std::vector<std::byte> bin(48 * 3 + 32 + 24);
    std::memcpy(bin.data(), left, 48);
    std::memcpy(bin.data() + 48, right, 48);
    std::memcpy(bin.data() + 96, normals, 48);
    std::memcpy(bin.data() + 144, uvs, 32);
    std::memcpy(bin.data() + 176, indices, 24);
    bin.insert(bin.end(), png.begin(), png.end());
    const std::string json = std::string{"{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"}
        + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":2,\"TEXCOORD_0\":3},\"indices\":4,\"material\":0},"
        + "{\"attributes\":{\"POSITION\":1,\"NORMAL\":2},\"indices\":4,\"material\":1}]}],"
        + "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}},"
        + "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,1,0,1]}}],"
        + "\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728,\"wrapS\":33071}],"
        + "\"images\":[{\"bufferView\":5,\"mimeType\":\"image/png\"}],"
        + "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        + "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        + "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        + "{\"bufferView\":3,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
        + "{\"bufferView\":4,\"componentType\":5125,\"count\":6,\"type\":\"SCALAR\"}],"
        + "\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
        + "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":144,\"byteLength\":32},"
        + "{\"buffer\":0,\"byteOffset\":176,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":200,\"byteLength\":"
        + std::to_string(png.size()) + "}],"
        + "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + ",\"uri\":\"data:application/octet-stream;base64,"
        + lykoware::test::base64(bin) + "\"}]}";
    std::vector<std::byte> file(json.size());
    std::memcpy(file.data(), json.data(), json.size());
    std::string load_error;
    const auto model = lykoware::gltf::read_model(file, {}, load_error);
    expect(model && model->parts.size() == 2 && model->materials.size() == 2 && model->images.size() == 1,
           "a model with two materials loads: " + load_error);
    if (!model) {
        return;
    }
    rhi::RhiError error;
    render::TextureSettings settings;
    settings.filter = rhi::TextureFilter::Nearest;
    settings.wrap = rhi::TextureWrap::ClampToEdge;
    settings.mipmaps = false;
    const auto texture = renderer.add_texture(model->images[0], settings, error);
    std::vector<render::MeshPart> parts;
    for (const auto& part : model->parts) {
        const auto& material = model->materials[*part.material];
        render::MeshPart mesh_part{part.first_index, part.index_count, {material.base_colour.x, material.base_colour.y, material.base_colour.z}};
        if (material.base_colour_texture && texture) {
            mesh_part.texture = *texture;
        }
        parts.push_back(mesh_part);
    }
    const auto mesh = texture ? renderer.add_mesh(model->mesh, parts, error) : std::nullopt;
    expect(mesh.has_value(), "the model's texture and parts are uploaded: " + error.message);
    const render::MeshPart outside{6, 9}; // indices 6..14 of 12
    expect(!renderer.add_mesh(model->mesh, std::span(&outside, 1), error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a part outside the mesh's indices is refused");
    const render::MeshPart unknown{0, 6, {1.F, 1.F, 1.F}, render::TextureId{77}};
    expect(!renderer.add_mesh(model->mesh, std::span(&unknown, 1), error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a part with an unknown texture is refused");
    if (!mesh) {
        return;
    }
    ecs::World entities;
    const ecs::Entity entity = entities.create();
    entities.add<lykoware::Transform>(entity, lykoware::Transform{});
    entities.add<render::MeshRenderer>(entity, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F}, *mesh});
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const render::Camera camera{{0.0, 0.0, 6.0}, {0.0, 0.0, 0.0}};
    lykoware::Image image;
    if (!target || !renderer.draw(entities, camera, *target, sky, error) || !rhi::RenderTargetCapture{*target}.capture(image)) {
        expect(false, "the two-material model draws: " + error.message);
        return;
    }
    expect(renderer.last_draw_count() == 1, "a mesh of two parts counts as one entity drawn");
    const math::Vec3 light = math::normalize(math::Vec3{0.4F, 1.F, 0.3F});
    const int lit = static_cast<int>(std::lround((0.25F + 0.75F * light.z) * 255.F));
    auto near = [&](Pixel p, int r, int g, int b) { return std::abs(p.r - r) <= 2 && std::abs(p.g - g) <= 2 && std::abs(p.b - b) <= 2; };
    expect(near(at(image, pixel_of(camera, {-1.7, 0.0, 0.0})), lit, 0, 0) && near(at(image, pixel_of(camera, {-0.7, 0.0, 0.0})), 0, 0, lit),
           "the textured part shows its red|blue texture, lit");
    expect(near(at(image, pixel_of(camera, {1.2, 0.0, 0.0})), 0, lit, 0), "the other part shows its green factor, lit");
    std::error_code ignored;
    std::filesystem::create_directories(output, ignored);
    if (lykoware::write_ppm(image, output / "materials.ppm")) {
        std::cout << "wrote " << (output / "materials.ppm").string() << '\n';
    }
}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "usage: lykoware_render_model_test LIT_VERTEX.spv LIT_FRAGMENT.spv OUTPUT_DIR\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.model test";
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

    Frame first;
    std::optional<render::MeshId> first_id;
    std::vector<std::uint32_t> vertex_words;
    std::vector<std::uint32_t> fragment_words;
    if (!rhi::read_spirv_file(argv[1], vertex_words, error) || !rhi::read_spirv_file(argv[2], fragment_words, error)) {
        std::cerr << "FAIL: the lit shaders load: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex_words, fragment_words, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        std::string load_error;
        const auto mesh = ramp(load_error);
        expect(mesh.has_value(), "the ramp model loads: " + load_error);
        const auto id = mesh ? renderer->add_mesh(*mesh, error) : std::nullopt;
        expect(id.has_value() && !(*id == render::MeshId{}), "the loaded mesh is uploaded");
        first_id = id;
        expect(!renderer->add_mesh(lykoware::MeshData{}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
               "an empty mesh is refused");
        if (id) {
            first = draw(*device, *renderer, *id, {});
            check(first, {});
            const math::DVec3 far{16000.0, 0.0, -16000.0};
            const Frame distant = draw(*device, *renderer, *id, far);
            check(distant, far);
            expect(distant.image.width() == width && lykoware::compare_images(first.image, distant.image, 0).matches,
                   "the model 16 km from the origin gives exactly the same image");
        }
        test_materials(*device, *renderer, argv[3]);
    }
    if (first_id) {
        // ADR 0030: an id works only with the renderer that made it. A renderer made after the
        // first is gone gives its own first mesh the same position.
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex_words, fragment_words, error);
        std::string load_error;
        const auto mesh = ramp(load_error);
        const auto id = renderer && mesh ? renderer->add_mesh(*mesh, error) : std::nullopt;
        expect(id && id->value == first_id->value && !(*id == *first_id),
               "a new renderer's first mesh id differs from the old renderer's only by owner");
        if (renderer) {
            const Frame stale = draw(*device, *renderer, *first_id, {});
            expect(stale.drawn + 1 == first.drawn, "an old renderer's mesh id is not drawn by a new renderer");
        }
    }
    if (instance->validation_error_count() != 0) {
        expect(false, std::to_string(instance->validation_error_count()) + " Vulkan validation errors");
    }

    const std::filesystem::path output = argv[3];
    std::error_code ignored;
    std::filesystem::create_directories(output, ignored);
    if (first.image.width() != 0 && lykoware::write_ppm(first.image, output / "model.ppm")) {
        std::cout << "wrote " << (output / "model.ppm").string() << '\n';
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " model assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render model passed\n";
    return EXIT_SUCCESS;
}
