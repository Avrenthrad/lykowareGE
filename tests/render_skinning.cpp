// Real glTF -> clip -> palette -> CPU reference deformation -> Vulkan capture (ADR 0051).
#include <lykoware/animation.hpp>
#include <lykoware/ecs.hpp>
#include <lykoware/gltf.hpp>
#include <lykoware/image.hpp>
#include <lykoware/render.hpp>
#include <lykoware/skinning.hpp>

#include "support/gltf_builder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace animation = lykoware::animation;
namespace math = lykoware::math;
namespace render = lykoware::render;
namespace rhi = lykoware::rhi;
int failures = 0;
constexpr std::uint32_t width = 160;
constexpr std::uint32_t height = 120;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::optional<lykoware::gltf::SkinnedModel> model(std::string& error)
{
    gltf_test::Model file;
    const auto positions = file.vec3s({{-1.05F, -0.3F, 0.F}, {-0.45F, -0.3F, 0.F},
                                      {-0.45F, 0.3F, 0.F}, {-1.05F, 0.3F, 0.F}});
    const auto normals = file.vec3s(std::vector<math::Vec3>(4, {0.F, 0.F, 1.F}));
    const auto uvs = file.vec2s({{0.F, 1.F}, {1.F, 1.F}, {1.F, 0.F}, {0.F, 0.F}});
    const auto indices = file.indices(std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3});
    const auto joints = file.accessor(file.view(std::vector<std::array<std::uint16_t, 4>>(4, {0, 1, 0, 0})), 5123, 4, "VEC4");
    const auto weights = file.accessor(file.view(std::vector<math::Vec4>(4, {0.5F, 0.5F, 0.F, 0.F})), 5126, 4, "VEC4");
    const auto times = file.accessor(file.view(std::vector<float>{0.F, 1.F}), 5126, 2, "SCALAR");
    const auto moves = file.vec3s({{0.F, 0.F, 0.F}, {3.F, 0.F, 0.F}});
    file.meshes = "[{\"primitives\":[{\"attributes\":{\"POSITION\":" + std::to_string(positions)
        + ",\"NORMAL\":" + std::to_string(normals) + ",\"TEXCOORD_0\":" + std::to_string(uvs)
        + ",\"JOINTS_0\":" + std::to_string(joints) + ",\"WEIGHTS_0\":" + std::to_string(weights)
        + "},\"indices\":" + std::to_string(indices) + "}]}]";
    file.nodes = "[{\"mesh\":0,\"skin\":0},{\"children\":[2]},{}]";
    file.scenes = "[{\"nodes\":[0,1]}]";
    file.extra = ",\"skins\":[{\"joints\":[1,2]}],\"animations\":[{\"name\":\"slide\",\"samplers\":[{\"input\":"
        + std::to_string(times) + ",\"output\":" + std::to_string(moves)
        + "}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}}]}]";
    return lykoware::gltf::read_skinned(file.gltf(), {}, error);
}

std::array<int, 3> pixel(const lykoware::Image& image, const render::Camera& camera, float x)
{
    const auto view = math::look_at({}, math::relative_to(camera.target, camera.eye), camera.up);
    const auto projected = math::perspective(camera.vertical_fov, static_cast<float>(width) / static_cast<float>(height), camera.near)
                         * view * math::Vec4{x, 0.F, -4.F, 1.F};
    const auto px = static_cast<std::uint32_t>((projected.x / projected.w * 0.5F + 0.5F) * static_cast<float>(width));
    const auto py = static_cast<std::uint32_t>((projected.y / projected.w * 0.5F + 0.5F) * static_cast<float>(height));
    const auto offset = (static_cast<std::size_t>(py) * width + px) * 3;
    const auto data = image.pixels();
    return {data[offset], data[offset + 1], data[offset + 2]};
}

bool exercise(rhi::Device& device, render::Renderer& renderer, const std::filesystem::path& output)
{
    std::string error;
    const auto source = model(error);
    expect(source && source->clips.size() == 1, "the generated two-joint glTF loads: " + error);
    if (!source || source->clips.size() != 1) {
        return false;
    }
    const auto bind_before = source->model.mesh.positions;
    auto pose = animation::rest_pose(source->skeleton);
    std::vector<math::Mat4> matrices;
    animation::skinning_matrices(source->skeleton, pose, matrices);
    lykoware::MeshData rest, moved;
    if (!animation::deform_mesh(source->model.mesh, source->joints, source->weights, matrices, rest, error)) {
        expect(false, error); return false;
    }
    animation::sample(source->clips[0], 1.F, false, pose);
    animation::skinning_matrices(source->skeleton, pose, matrices);
    if (!animation::deform_mesh(source->model.mesh, source->joints, source->weights, matrices, moved, error)) {
        expect(false, error); return false;
    }
    auto manual = source->model.mesh;
    for (auto& point : manual.positions) {
        point.x += 1.5F;
    }
    expect(moved.positions == manual.positions, "the sampled pose agrees with hand-calculated weighted displacement");
    expect(source->model.mesh.positions == bind_before, "sampling and deforming leave the bind model unchanged");

    rhi::RhiError gpu_error;
    const lykoware::RgbaImage image{2, 1, {255, 0, 0, 255, 0, 0, 255, 255}};
    render::TextureSettings sampling;
    sampling.filter = rhi::TextureFilter::Nearest;
    sampling.mipmaps = false;
    const auto texture = renderer.add_texture(image, sampling, gpu_error);
    const auto rest_id = renderer.add_mesh(rest, gpu_error);
    const auto moved_id = renderer.add_mesh(moved, gpu_error);
    const auto manual_id = renderer.add_mesh(manual, gpu_error);
    auto target = rhi::RenderTarget::create(device, {width, height, true}, gpu_error);
    if (!texture || !rest_id || !moved_id || !manual_id || !target) {
        expect(false, "skin reference uploads: " + gpu_error.message); return false;
    }
    lykoware::ecs::World world;
    const auto entity = world.create();
    world.add<lykoware::Transform>(entity, lykoware::Transform{});
    render::MeshRenderer component;
    component.mesh = *rest_id;
    component.texture = *texture;
    component.colour = {1.F, 1.F, 1.F};
    world.add<render::MeshRenderer>(entity, component);
    const render::Camera camera{{0.0, 0.0, 4.0}, {0.0, 0.0, 0.0}};
    auto capture = [&](render::MeshId mesh, lykoware::Image& result) {
        world.get<render::MeshRenderer>(entity)->mesh = mesh;
        const bool ok = renderer.draw(world, camera, *target, {0.F, 0.F, 0.F, 1.F}, gpu_error)
                     && rhi::RenderTargetCapture{*target}.capture(result);
        expect(ok, "reference pose capture: " + gpu_error.message);
        return ok;
    };
    lykoware::Image rest_image, moved_image, manual_image;
    if (!capture(*rest_id, rest_image) || !capture(*moved_id, moved_image) || !capture(*manual_id, manual_image)) {
        return false;
    }
    expect(lykoware::compare_images(moved_image, manual_image, 0).matches, "deformed and independently calculated meshes render identical pixels");
    const auto dark = std::array<int, 3>{0, 0, 0};
    expect(pixel(rest_image, camera, -0.75F) != dark && pixel(moved_image, camera, -0.75F) == dark,
           "the animated surface leaves its rest location");
    const auto red = pixel(moved_image, camera, 0.60F);
    const auto blue = pixel(moved_image, camera, 0.90F);
    expect(red[0] > 40 && red[1] == 0 && red[2] == 0 && blue[2] > 40 && blue[0] == 0 && blue[1] == 0,
           "the deformed surface arrives in the expected location with its red/blue UV texture intact");
    const auto second = world.create();
    world.add<lykoware::Transform>(second, lykoware::Transform{});
    component.mesh = *rest_id;
    world.add<render::MeshRenderer>(second, component);
    lykoware::Image both;
    if (!capture(*moved_id, both)) {
        return false;
    }
    expect(renderer.last_draw_count() == 2 && pixel(both, camera, -0.75F) != dark && pixel(both, camera, 0.75F) != dark,
           "two snapshots of one bind mesh keep independent poses in the same draw");
    std::filesystem::create_directories(output);
    expect(lykoware::write_ppm(rest_image, output / "skin-rest.ppm")
               && lykoware::write_ppm(moved_image, output / "skin-posed.ppm")
               && lykoware::write_ppm(both, output / "skin-independent.ppm"), "diagnostic captures are saved");
    return failures == 0;
}

}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: render_skinning VERTEX.spv FRAGMENT.spv OUTPUT_DIR\n";
        return 1;
    }
    rhi::InstanceConfig config;
    config.application_name = "Lykoware skinning reference test";
    config.validation = rhi::Validation::IfAvailable;
    rhi::RhiError error;
    auto instance = rhi::Instance::create(config, error);
    if (!instance) {
        const bool unavailable = error.failure == rhi::RhiFailure::NoLoader || error.failure == rhi::RhiFailure::NoDriver
                              || error.failure == rhi::RhiFailure::VersionTooOld;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? 77 : 1;
    }
    auto device = rhi::Device::create(*instance, {}, error);
    if (!device) {
        std::cout << error.message << '\n';
        return error.failure == rhi::RhiFailure::NoSuitableDevice ? 77 : 1;
    }
    std::vector<std::uint32_t> vertex, fragment;
    if (!rhi::read_spirv_file(argv[1], vertex, error) || !rhi::read_spirv_file(argv[2], fragment, error)) {
        std::cerr << error.message << '\n'; return 1;
    }
    bool passed = false;
    {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex, fragment, error);
        passed = renderer && exercise(*device, *renderer, argv[3]);
        if (!renderer) { std::cerr << error.message << '\n'; }
    }
    return passed && instance->validation_error_count() == 0 ? 0 : 1;
}
