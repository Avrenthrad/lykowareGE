// rhi.mesh: lit 3D meshes through the RHI (ADR 0025). Vertex and index
// buffers, a depth buffer with reversed depth, back-face culling, push
// constants, and camera-relative positions from the maths module (ADR 0024).
// Draws offscreen and checks pixels through the image harness, sampling well
// inside each face so different GPUs' edge rasterisation does not matter.
//
// Usage: lykoware_rhi_mesh_test LIT_VERTEX.spv LIT_FRAGMENT.spv TRIANGLE_VERTEX.spv
//                               TRIANGLE_FRAGMENT.spv OUTPUT_DIR
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable
// device). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace math = lykoware::math;
namespace rhi = lykoware::rhi;

constexpr int exit_skip = 77;
constexpr std::uint32_t size = 64;
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

Pixel at(const lykoware::Image& image, std::uint32_t x, std::uint32_t y)
{
    const auto pixels = image.pixels();
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width() + x) * 3;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2]};
}

bool about(int value, double expected, int tolerance = 3)
{
    return std::abs(value - static_cast<int>(std::lround(expected))) <= tolerance;
}

// The shader's lighting for a face with normal n: 0.25 + 0.75 * max(0, n . light).
double lit(math::Vec3 n)
{
    const math::Vec3 light = math::normalize(math::Vec3{0.4F, 1.F, 0.3F});
    return 0.25 + 0.75 * std::fmax(0.0, static_cast<double>(math::dot(n, light)));
}

// --- Meshes ---------------------------------------------------------------------------------

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
};
static_assert(sizeof(Vertex) == rhi::position_normal_stride);

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// A unit cube centred on the origin: four vertices per face with the face normal, two
// triangles per face, counter-clockwise when seen from outside.
Mesh cube()
{
    struct Face {
        math::Vec3 n;
        math::Vec3 u;
        math::Vec3 v;
    };
    const Face faces[6] = {
        {{1.F, 0.F, 0.F}, {0.F, 0.F, -1.F}, {0.F, 1.F, 0.F}}, {{-1.F, 0.F, 0.F}, {0.F, 0.F, 1.F}, {0.F, 1.F, 0.F}},
        {{0.F, 1.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 0.F, -1.F}}, {{0.F, -1.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 0.F, 1.F}},
        {{0.F, 0.F, 1.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}},  {{0.F, 0.F, -1.F}, {-1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}},
    };
    Mesh mesh;
    for (const Face& face : faces) {
        // Seen from outside (looking along -n), u is right and v is up exactly when u x v = n.
        expect(math::cross(face.u, face.v) == face.n, "each cube face's corners run counter-clockwise from outside");
        const math::Vec3 centre = face.n * 0.5F;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& [su, sv] : {std::pair{-0.5F, -0.5F}, std::pair{0.5F, -0.5F}, std::pair{0.5F, 0.5F},
                                     std::pair{-0.5F, 0.5F}}) {
            mesh.vertices.push_back({centre + face.u * su + face.v * sv, face.n});
        }
        for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
            mesh.indices.push_back(base + corner);
        }
    }
    return mesh;
}

template <class T>
std::span<const std::byte> bytes_of(const std::vector<T>& values)
{
    return std::as_bytes(std::span<const T>(values));
}

// --- Camera and push constants --------------------------------------------------------------

// Matches tests/shaders/mesh.slang.
struct Constants {
    math::Mat4 clip;
    math::Vec4 normal[3];
    math::Vec4 colour;
};
static_assert(sizeof(Constants) == rhi::push_constant_max_bytes);

struct Camera {
    math::DVec3 eye;
    math::DVec3 target;
};

// Constants for an object at a world position. Everything is made relative to the camera
// before it becomes float (math::relative_to), so the result does not depend on where in the
// world the scene is.
Constants constants_for(const Camera& camera, math::DVec3 position, math::Quat rotation, math::Vec3 scale,
                        math::Vec3 colour)
{
    const math::Mat4 view =
        math::look_at({0.F, 0.F, 0.F}, math::relative_to(camera.target, camera.eye), {0.F, 1.F, 0.F});
    const math::Mat4 projection = math::perspective(std::numbers::pi_v<float> / 3.F, 1.F, 0.1F);
    const math::Mat4 model = math::trs(math::relative_to(position, camera.eye), rotation, scale);
    Constants constants;
    constants.clip = projection * view * model;
    const math::Mat4 normal = math::rotation(rotation);
    // The lit shader's light rides in the normal columns' w and its ambient in colour.a (ADR 0082).
    const float light[3] = {0.4F, 1.F, 0.3F};
    for (int column = 0; column < 3; ++column) {
        constants.normal[column] = {normal.m[column][0], normal.m[column][1], normal.m[column][2], light[column]};
    }
    constants.colour = {colour.x, colour.y, colour.z, 0.25F};
    return constants;
}

// The pixel a world point lands on.
std::pair<std::uint32_t, std::uint32_t> pixel_of(const Camera& camera, math::DVec3 point)
{
    const Constants c = constants_for(camera, point, {}, {1.F, 1.F, 1.F}, {});
    const math::Vec4 clip = c.clip * math::Vec4{0.F, 0.F, 0.F, 1.F};
    const float x = (clip.x / clip.w * 0.5F + 0.5F) * static_cast<float>(size);
    const float y = (clip.y / clip.w * 0.5F + 0.5F) * static_cast<float>(size);
    return {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
}

struct Scene {
    std::unique_ptr<rhi::Buffer> vertices;
    std::unique_ptr<rhi::Buffer> indices;
    std::unique_ptr<rhi::Buffer> front_face;
    std::unique_ptr<rhi::Buffer> front_face_reversed;
    std::unique_ptr<rhi::GraphicsPipeline> pipeline;
    std::unique_ptr<rhi::RenderTarget> target;
};

const rhi::ClearColor clear_colour{0.1F, 0.1F, 0.1F, 1.F};
constexpr int clear_byte = 26; // 0.1 * 255

bool render(Scene& scene, const std::vector<Constants>& constants, const std::vector<const rhi::Buffer*>& indices,
            lykoware::Image& image)
{
    std::vector<rhi::MeshDraw> draws;
    for (std::size_t i = 0; i < constants.size(); ++i) {
        draws.push_back({scene.vertices.get(), indices[i], 0, std::as_bytes(std::span(&constants[i], 1))});
    }
    rhi::RhiError error;
    if (!scene.target->draw_meshes(clear_colour, *scene.pipeline, draws, error)) {
        std::cerr << "  draw_meshes: " << error.message << '\n';
        return false;
    }
    return rhi::RenderTargetCapture{*scene.target}.capture(image);
}

// --- Tests ----------------------------------------------------------------------------------

void test_lit_cube(Scene& scene, lykoware::Image& saved)
{
    const Camera camera{{0.0, 0.0, 3.0}, {0.0, 0.0, 0.0}};
    lykoware::Image image;
    const std::vector<Constants> one{constants_for(camera, {}, {}, {1.F, 1.F, 1.F}, {1.F, 0.F, 0.F})};
    expect(render(scene, one, {scene.indices.get()}, image), "a cube draws");
    saved = image;
    const Pixel centre = at(image, size / 2, size / 2);
    const Pixel corner = at(image, 1, 1);
    expect(about(centre.r, 255.0 * lit({0.F, 0.F, 1.F})) && centre.g == 0 && centre.b == 0,
           "the camera sees the cube's front face, lit for its normal (back-face culling keeps the outside)");
    expect(corner.r == clear_byte && corner.g == clear_byte && corner.b == clear_byte,
           "outside the cube keeps the clear colour");

    // From (2, 2, 2) the camera sees the top, right and front faces, each lit for its normal.
    const Camera above{{2.0, 2.0, 2.0}, {0.0, 0.0, 0.0}};
    const std::vector<Constants> green{constants_for(above, {}, {}, {1.F, 1.F, 1.F}, {0.F, 1.F, 0.F})};
    expect(render(scene, green, {scene.indices.get()}, image), "the cube draws from above");
    const auto [tx, ty] = pixel_of(above, {0.0, 0.5, 0.0});
    const auto [rx, ry] = pixel_of(above, {0.5, 0.0, 0.0});
    const auto [fx, fy] = pixel_of(above, {0.0, 0.0, 0.5});
    expect(about(at(image, tx, ty).g, 255.0 * lit({0.F, 1.F, 0.F})), "the top face has its lighting");
    expect(about(at(image, rx, ry).g, 255.0 * lit({1.F, 0.F, 0.F})), "the right face has its lighting");
    expect(about(at(image, fx, fy).g, 255.0 * lit({0.F, 0.F, 1.F})), "the front face has its lighting");
    expect(ty < ry && ty < fy, "the top face is higher in the image (Y is up)");
    expect(rx > fx, "the +X face is to the right of the +Z face, as seen from (2, 2, 2)");
}

void test_depth(Scene& scene)
{
    const Camera camera{{0.0, 0.0, 4.0}, {0.0, 0.0, 0.0}};
    const Constants near = constants_for(camera, {0.0, 0.0, 1.0}, {}, {1.F, 1.F, 1.F}, {1.F, 0.F, 0.F});
    // A 4 m cube behind, its front face at z = 0.
    const Constants far = constants_for(camera, {0.0, 0.0, -2.0}, {}, {4.F, 4.F, 4.F}, {0.F, 0.F, 1.F});
    lykoware::Image near_first;
    lykoware::Image far_first;
    expect(render(scene, {near, far}, {scene.indices.get(), scene.indices.get()}, near_first), "two cubes draw");
    expect(render(scene, {far, near}, {scene.indices.get(), scene.indices.get()}, far_first), "and in reverse order");
    const Pixel a = at(near_first, size / 2, size / 2);
    const Pixel b = at(far_first, size / 2, size / 2);
    expect(a.r > 50 && a.b == 0 && b.r > 50 && b.b == 0, "the nearer cube wins whatever the drawing order");
    // A point on the far cube's front face that the near cube does not hide.
    const auto [ex, ey] = pixel_of(camera, {0.0, 1.5, 0.0});
    const Pixel edge = at(near_first, ex, ey);
    expect(edge.b > 50 && edge.r == 0, "the bigger cube behind shows around the nearer one");
}

void test_winding(Scene& scene)
{
    const Camera camera{{0.0, 0.0, 3.0}, {0.0, 0.0, 0.0}};
    const std::vector<Constants> one{constants_for(camera, {}, {}, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F})};
    lykoware::Image facing;
    lykoware::Image reversed;
    expect(render(scene, one, {scene.front_face.get()}, facing), "the front face alone draws");
    expect(render(scene, one, {scene.front_face_reversed.get()}, reversed), "and with its winding reversed");
    expect(at(facing, size / 2, size / 2).r > 50, "a face counter-clockwise as seen by the camera is drawn");
    expect(at(reversed, size / 2, size / 2).r == clear_byte, "the same face wound clockwise is culled");
}

void test_far_from_origin(Scene& scene, const lykoware::Image& at_origin)
{
    // The first scene moved 16 km away: camera-relative maths gives the same floats, so the
    // same bytes.
    const math::DVec3 offset{16000.0, 300.0, -16000.0};
    const Camera camera{offset + math::Vec3{0.F, 0.F, 3.F}, offset};
    lykoware::Image image;
    const std::vector<Constants> one{constants_for(camera, offset, {}, {1.F, 1.F, 1.F}, {1.F, 0.F, 0.F})};
    expect(render(scene, one, {scene.indices.get()}, image), "the cube draws 16 km from the origin");
    expect(lykoware::compare_images(image, at_origin, 0).matches, "it looks exactly as it does at the origin");
}

void test_gpu_time(const rhi::Device& device, Scene& scene)
{
    // ADR 0039: a mesh draw is timed on the GPU, and a refused one leaves no time behind.
    const Camera camera{{0.0, 0.0, 3.0}, {0.0, 0.0, 0.0}};
    lykoware::Image image;
    const std::vector<Constants> one{constants_for(camera, {}, {}, {1.F, 1.F, 1.F}, {1.F, 0.F, 0.F})};
    expect(render(scene, one, {scene.indices.get()}, image), "a cube draws for timing");
    const std::optional<std::uint64_t> drawn = scene.target->last_gpu_time_ns();
    expect(drawn.has_value() == device.gpu_timestamps(), "a mesh draw has a GPU time when the device has timestamps");
    expect(!drawn || (*drawn > 0 && *drawn < 10'000'000'000ULL), "the mesh draw's GPU time is plausible");
    rhi::RhiError error;
    const Constants constants{};
    const rhi::MeshDraw no_vertices[1] = {{nullptr, scene.indices.get(), 0, std::as_bytes(std::span(&constants, 1))}};
    expect(!scene.target->draw_meshes(clear_colour, *scene.pipeline, no_vertices, error),
           "a mesh without vertices is refused");
    expect(!scene.target->last_gpu_time_ns(), "a refused draw clears the last GPU time");
}

void test_misuse(const rhi::Device& device, Scene& scene, const rhi::Shader& vertex, const rhi::Shader& fragment,
                 const rhi::Shader& triangle_vertex, const rhi::Shader& triangle_fragment)
{
    rhi::RhiError error;
    const std::vector<std::byte> empty;
    expect(rhi::Buffer::create(device, rhi::BufferUsage::Vertex, empty, error) == nullptr
               && error.failure == rhi::RhiFailure::InvalidArgument,
           "an empty buffer is refused");
    const std::vector<std::byte> six_bytes(6);
    expect(rhi::Buffer::create(device, rhi::BufferUsage::Index, six_bytes, error) == nullptr
               && error.failure == rhi::RhiFailure::InvalidArgument,
           "index data that is not whole 32-bit indices is refused");

    rhi::GraphicsPipelineDesc desc;
    desc.vertex = &vertex;
    desc.fragment = &fragment;
    desc.vertex_layout = rhi::VertexLayout::PositionNormal;
    desc.push_constant_bytes = 130;
    expect(rhi::GraphicsPipeline::create(device, desc, error) == nullptr
               && error.failure == rhi::RhiFailure::InvalidArgument,
           "push constants over 128 bytes are refused");

    const Constants constants{};
    const auto push = std::as_bytes(std::span(&constants, 1));
    auto refused = [&](const rhi::MeshDraw& draw, std::string_view what) {
        const rhi::MeshDraw draws[1] = {draw};
        expect(!scene.target->draw_meshes(clear_colour, *scene.pipeline, draws, error)
                   && error.failure == rhi::RhiFailure::InvalidArgument,
               what);
    };
    refused({nullptr, scene.indices.get(), 0, push}, "a mesh without vertices is refused");
    refused({scene.vertices.get(), nullptr, 0, push}, "a mesh without indices is refused");
    refused({scene.indices.get(), scene.indices.get(), 0, push}, "an index buffer used as vertices is refused");
    refused({scene.vertices.get(), scene.indices.get(), 4, push}, "an index count that is not whole triangles is refused");
    refused({scene.vertices.get(), scene.indices.get(), 39, push}, "more indices than the buffer holds is refused");
    refused({scene.vertices.get(), scene.indices.get(), 0, push.first(64)}, "the wrong push-constant size is refused");
    const std::vector<std::uint32_t> past_end{0, 1, 24};
    auto bad_indices = rhi::Buffer::create(device, rhi::BufferUsage::Index, bytes_of(past_end), error);
    refused({scene.vertices.get(), bad_indices.get(), 0, push}, "an index past the last vertex is refused");

    desc.push_constant_bytes = rhi::push_constant_max_bytes;
    desc.depth = true;
    auto depth_pipeline = rhi::GraphicsPipeline::create(device, desc, error);
    auto flat_target = rhi::RenderTarget::create(device, {size, size, false}, error);
    const rhi::MeshDraw ok[1] = {{scene.vertices.get(), scene.indices.get(), 0, push}};
    expect(depth_pipeline && flat_target && !flat_target->draw_meshes(clear_colour, *depth_pipeline, ok, error)
               && error.failure == rhi::RhiFailure::InvalidArgument,
           "a depth pipeline cannot draw into a target without depth");
    // The triangle shaders make their own vertices, so they suit a pipeline without vertex input.
    rhi::GraphicsPipelineDesc triangle_desc;
    triangle_desc.vertex = &triangle_vertex;
    triangle_desc.fragment = &triangle_fragment;
    triangle_desc.push_constant_bytes = rhi::push_constant_max_bytes;
    auto no_layout = rhi::GraphicsPipeline::create(device, triangle_desc, error);
    expect(no_layout && !scene.target->draw_meshes(clear_colour, *no_layout, ok, error)
               && error.failure == rhi::RhiFailure::InvalidArgument,
           "a pipeline without a vertex layout cannot draw meshes");
}

}

int main(int argc, char* argv[])
{
    if (argc != 6) {
        std::cerr << "usage: lykoware_rhi_mesh_test LIT_VERTEX.spv LIT_FRAGMENT.spv TRIANGLE_VERTEX.spv "
                     "TRIANGLE_FRAGMENT.spv OUTPUT_DIR\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path output = argv[5];

    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware rhi.mesh test";
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

    lykoware::Image cube_image;
    {
        auto vertex = rhi::Shader::load_file(*device, argv[1], rhi::ShaderStage::Vertex, error);
        auto fragment = rhi::Shader::load_file(*device, argv[2], rhi::ShaderStage::Fragment, error);
        auto triangle_vertex = rhi::Shader::load_file(*device, argv[3], rhi::ShaderStage::Vertex, error);
        auto triangle_fragment = rhi::Shader::load_file(*device, argv[4], rhi::ShaderStage::Fragment, error);
        if (!vertex || !fragment || !triangle_vertex || !triangle_fragment) {
            std::cerr << "FAIL: the lit shaders load: " << error.message << '\n';
            return EXIT_FAILURE;
        }

        Scene scene;
        const Mesh mesh = cube();
        scene.vertices = rhi::Buffer::create(*device, rhi::BufferUsage::Vertex, bytes_of(mesh.vertices), error);
        scene.indices = rhi::Buffer::create(*device, rhi::BufferUsage::Index, bytes_of(mesh.indices), error);
        // The +Z face is vertices 16..19.
        const std::vector<std::uint32_t> front{16, 17, 18, 16, 18, 19};
        const std::vector<std::uint32_t> front_reversed{16, 18, 17, 16, 19, 18};
        scene.front_face = rhi::Buffer::create(*device, rhi::BufferUsage::Index, bytes_of(front), error);
        scene.front_face_reversed = rhi::Buffer::create(*device, rhi::BufferUsage::Index, bytes_of(front_reversed), error);
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.vertex_layout = rhi::VertexLayout::PositionNormal;
        desc.depth = true;
        desc.cull_back_faces = true;
        desc.push_constant_bytes = rhi::push_constant_max_bytes;
        scene.pipeline = rhi::GraphicsPipeline::create(*device, desc, error);
        if (scene.pipeline) {
            scene.target = rhi::RenderTarget::create(*device, {size, size, true}, error);
        }
        if (!scene.vertices || !scene.indices || !scene.front_face || !scene.front_face_reversed || !scene.pipeline
            || !scene.target) {
            std::cerr << "FAIL: setup: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        expect(scene.target->has_depth(), "the target has a depth buffer");

        test_lit_cube(scene, cube_image);
        lykoware::Image again;
        expect(render(scene, {constants_for({{0.0, 0.0, 3.0}, {}}, {}, {}, {1.F, 1.F, 1.F}, {1.F, 0.F, 0.F})},
                      {scene.indices.get()}, again)
                   && lykoware::compare_images(again, cube_image, 0).matches,
               "drawing the same scene twice gives identical bytes");
        test_depth(scene);
        test_winding(scene);
        test_far_from_origin(scene, cube_image);
        test_gpu_time(*device, scene);
        test_misuse(*device, scene, *vertex, *fragment, *triangle_vertex, *triangle_fragment);
    }

    if (g_failures != 0) {
        std::error_code ignored;
        std::filesystem::create_directories(output, ignored);
        if (cube_image.width() != 0 && lykoware::write_ppm(cube_image, output / "cube-actual.ppm")) {
            std::cerr << "  wrote " << (output / "cube-actual.ppm").string() << '\n';
        }
        std::cerr << g_failures << " mesh assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "rhi mesh passed\n";
    return EXIT_SUCCESS;
}
