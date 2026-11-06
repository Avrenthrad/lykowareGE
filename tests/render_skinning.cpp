#include "animated_mannequin.hpp"

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
#include <limits>
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

bool mannequin_capture(rhi::Device& device, render::Renderer& renderer, const std::filesystem::path& output)
{
    const auto asset = training_mannequin::make();
    training_mannequin::Playback idle, walking;
    for (int i = 0; i < 60; ++i) {
        idle.advance(asset, 1.F / 60.F, 0.F);
        walking.advance(asset, 1.F / 60.F, 4.F);
    }
    rhi::RhiError error;
    const auto mesh = renderer.add_skinned_mesh(asset.mesh, asset.joints, asset.weights,
                                               asset.skeleton.joints.size(), asset.parts, error);
    auto target = rhi::RenderTarget::create(device, {320, 240, true}, error);
    if (!mesh || !target) { expect(false, "mannequin GPU setup: " + error.message); return false; }
    lykoware::ecs::World world;
    for (int i = 0; i < 2; ++i) {
        const auto entity = world.create();
        world.add<lykoware::Transform>(entity, lykoware::Transform{{i == 0 ? -0.65 : 0.65, 0.0, 0.0}, {}});
        world.add<render::MeshRenderer>(entity, render::MeshRenderer{render::MeshShape::Cube, {1.F,1.F,1.F}, {1.F,1.F,1.F}, *mesh});
        world.add<render::SkinPose>(entity, render::SkinPose{i == 0 ? idle.matrices : walking.matrices});
    }
    const render::Camera camera{{0.0, 1.1, 3.5}, {0.0, 0.85, 0.0}};
    lykoware::Image actual, reference;
    if (!renderer.draw(world, camera, *target, {0.025F,0.03F,0.045F,1.F}, error)
        || !rhi::RenderTargetCapture{*target}.capture(actual)) {
        expect(false, "mannequin GPU capture: " + error.message); return false;
    }
    std::string reason;
    bool valid = true;
    world.each<render::MeshRenderer, render::SkinPose>([&](lykoware::ecs::Entity, render::MeshRenderer& component, render::SkinPose& pose) {
        lykoware::MeshData deformed;
        if (!animation::deform_mesh(asset.mesh, asset.joints, asset.weights, pose.matrices, deformed, reason)) {
            valid = false; return;
        }
        const auto cpu = renderer.add_mesh(deformed, asset.parts, error);
        if (!cpu) { valid = false; return; }
        component.mesh = *cpu;
    });
    if (!valid || !renderer.draw(world, camera, *target, {0.025F,0.03F,0.045F,1.F}, error)
        || !rhi::RenderTargetCapture{*target}.capture(reference)) {
        expect(false, "mannequin CPU reference capture: " + error.message + reason); return false;
    }
    expect(lykoware::compare_images(reference, actual, 2).matches,
           "idle and walking mannequin GPU pixels match CPU-deformed geometry");
    expect(lykoware::write_ppm(actual, output / "mannequin.ppm"), "mannequin diagnostic capture saved");
    return true;
}

bool exercise(rhi::Device& device, render::Renderer& renderer, const std::filesystem::path& output, bool gpu)
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
    if (gpu) {
        if (!mannequin_capture(device, renderer, output)) { return false; }
        // Geometry is registered once; each following GPU draw changes only SkinPose.
        const auto skinned = renderer.add_skinned_mesh(source->model.mesh, source->joints, source->weights,
                                                       source->skeleton.joints.size(), {}, gpu_error);
        expect(skinned.has_value(), "the bind mesh and influences upload once: " + gpu_error.message);
        if (!skinned) { return false; }
        world.destroy(second);
        lykoware::Image bind_gpu;
        if (!capture(*skinned, bind_gpu)) { return false; }
        expect(lykoware::compare_images(rest_image, bind_gpu, 2).matches, "missing SkinPose uses the identity palette/bind geometry");
        world.add<render::SkinPose>(entity, render::SkinPose{matrices});
        lykoware::Image gpu_image;
        if (!capture(*skinned, gpu_image)) { return false; }
        expect(lykoware::compare_images(moved_image, gpu_image, 2).matches, "GPU weighted translation matches CPU reference pixels");
        // Rotation + non-uniform scale exercise column order and inverse-transpose normals.
        pose = animation::rest_pose(source->skeleton);
        pose[0].scale = {1.2F, 0.8F, 1.F};
        pose[0].rotation = math::from_axis_angle({0.F, 1.F, 0.F}, 0.4F);
        pose[1].rotation = math::from_axis_angle({0.F, 0.F, 1.F}, 0.35F);
        animation::skinning_matrices(source->skeleton, pose, matrices);
        if (!animation::deform_mesh(source->model.mesh, source->joints, source->weights, matrices, moved, error)) {
            expect(false, error); return false;
        }
        const auto reference = renderer.add_mesh(moved, gpu_error);
        if (!reference) { expect(false, gpu_error.message); return false; }
        world.get<render::SkinPose>(entity)->matrices = matrices;
        lykoware::Image cpu_rotated, gpu_rotated;
        if (!capture(*reference, cpu_rotated) || !capture(*skinned, gpu_rotated)) { return false; }
        expect(lykoware::compare_images(cpu_rotated, gpu_rotated, 2).matches,
               "GPU rotated/non-uniform blend matches CPU reference, including lit normals");
        // Repeat many in-place pose changes against one immutable GPU mesh.
        for (int frame = 0; frame < 32; ++frame) {
            pose = animation::rest_pose(source->skeleton);
            animation::sample(source->clips[0], frame % 2 == 0 ? 0.F : 1.F, false, pose);
            animation::skinning_matrices(source->skeleton, pose, world.get<render::SkinPose>(entity)->matrices);
            if (!capture(*skinned, gpu_image)) { return false; }
            const auto& expected = frame % 2 == 0 ? rest_image : moved_image;
            expect(lykoware::compare_images(expected, gpu_image, 2).matches, "reused palette has no stale pose");
        }
        const auto neighbour = world.create();
        world.add<lykoware::Transform>(neighbour, lykoware::Transform{});
        component.mesh = *skinned;
        world.add<render::MeshRenderer>(neighbour, component);
        world.add<render::SkinPose>(neighbour, render::SkinPose{std::vector<math::Mat4>(matrices.size())});
        if (!capture(*skinned, gpu_image)) { return false; }
        expect(lykoware::compare_images(both, gpu_image, 2).matches, "GPU instances of one mesh have independent palettes");
        world.destroy(neighbour);
        auto& palette = world.get<render::SkinPose>(entity)->matrices;
        const auto valid = palette;
        palette.clear();
        expect(!renderer.draw(world, camera, *target, {}, gpu_error) && gpu_error.failure == rhi::RhiFailure::InvalidArgument,
               "wrong pose count is refused before submission");
        palette = valid; palette[0].m[0][0] = std::numeric_limits<float>::quiet_NaN();
        expect(!renderer.draw(world, camera, *target, {}, gpu_error) && gpu_error.failure == rhi::RhiFailure::InvalidArgument,
               "non-finite palette is refused");
        palette = valid; palette[0].m[0][3] = 0.1F;
        expect(!renderer.draw(world, camera, *target, {}, gpu_error) && gpu_error.failure == rhi::RhiFailure::InvalidArgument,
               "projective palette is refused");
        palette = valid;
        if (!capture(*skinned, gpu_image)) { return false; }
        expect(lykoware::compare_images(moved_image, gpu_image, 2).matches, "valid drawing resumes after invalid palettes");
        auto bad_weights = source->weights; bad_weights[0] = {};
        expect(!renderer.add_skinned_mesh(source->model.mesh, source->joints, bad_weights, matrices.size(), {}, gpu_error),
               "zero weights are refused");
        bad_weights = source->weights; bad_weights[0].x = -1.F;
        expect(!renderer.add_skinned_mesh(source->model.mesh, source->joints, bad_weights, matrices.size(), {}, gpu_error),
               "negative weights are refused");
        auto bad_joints = source->joints; bad_joints[0][0] = 256;
        expect(!renderer.add_skinned_mesh(source->model.mesh, bad_joints, source->weights, matrices.size(), {}, gpu_error),
               "out-of-range joints are refused");
        // A singular blend uses the declared bind-normal fallback, not NaNs or cofactors.
        math::Mat4 singular;
        singular.m[1][2] = 0.5F;
        singular.m[2][1] = 2.F;
        auto collapsed = source->model.mesh;
        for (auto& point : collapsed.positions) { point.z = 0.5F * point.y; }
        const auto fallback_reference = renderer.add_mesh(collapsed, gpu_error);
        if (!fallback_reference) { expect(false, gpu_error.message); return false; }
        palette.assign(2, singular);
        lykoware::Image fallback_cpu, fallback_gpu;
        if (!capture(*fallback_reference, fallback_cpu) || !capture(*skinned, fallback_gpu)) { return false; }
        expect(lykoware::compare_images(fallback_cpu, fallback_gpu, 2).matches, "singular blend uses finite bind normals");
        palette = valid;
        const render::MeshPart malformed_part{0, 6, {1.F,1.F,1.F}, {0, texture->owner}};
        expect(!renderer.add_mesh(source->model.mesh, std::span(&malformed_part, 1), gpu_error)
            && !renderer.add_skinned_mesh(source->model.mesh, source->joints, source->weights, matrices.size(),
                                          std::span(&malformed_part, 1), gpu_error), "zero-valued owned part textures are refused");
        // Slot 255 must reach the final uniform matrix, not a truncated byte offset.
        auto last_joints = source->joints;
        for (auto& ids : last_joints) { ids[1] = 255; }
        const auto last_mesh = renderer.add_skinned_mesh(source->model.mesh, last_joints, source->weights, 256, {}, gpu_error);
        expect(last_mesh.has_value(), "256-joint mesh uploads");
        if (!last_mesh) { return false; }
        palette.assign(256, math::Mat4{}); palette[255] = math::translation({3.F, 0.F, 0.F});
        if (!capture(*last_mesh, gpu_image)) { return false; }
        expect(lykoware::compare_images(moved_image, gpu_image, 2).matches, "joint 255 reads the final palette matrix");
        // Grow the arena, and then shrink the draw list without retaining another instance's pose.
        std::vector<lykoware::ecs::Entity> extras;
        component.mesh = *skinned;
        for (int i = 0; i < 18; ++i) {
            const auto extra = world.create(); extras.push_back(extra);
            world.add<lykoware::Transform>(extra, lykoware::Transform{{100.0 + i, 0.0, 0.0}, {}});
            world.add<render::MeshRenderer>(extra, component);
            world.add<render::SkinPose>(extra, render::SkinPose{valid});
        }
        if (!capture(*last_mesh, gpu_image)) { return false; }
        for (auto extra : extras) { world.destroy(extra); }
        if (!capture(*last_mesh, gpu_image)) { return false; }
        expect(lykoware::compare_images(moved_image, gpu_image, 2).matches, "arena growth and reuse preserve the current palette");
        extras.clear();
        // Refuse the arena cap before GPU submission; later valid frames must still work.
        for (std::size_t i = 0; i < rhi::skin_palette_max_draws; ++i) {
            const auto extra = world.create(); extras.push_back(extra);
            world.add<lykoware::Transform>(extra, lykoware::Transform{});
            world.add<render::MeshRenderer>(extra, component);
            world.add<render::SkinPose>(extra, render::SkinPose{valid});
        }
        expect(!renderer.draw(world, camera, *target, {}, gpu_error) && gpu_error.failure == rhi::RhiFailure::InvalidArgument,
               "palette arena cap is refused before submission");
        for (auto extra : extras) { if (world.alive(extra)) { world.destroy(extra); } }
        if (!capture(*last_mesh, gpu_image)) { return false; }
        expect(lykoware::compare_images(moved_image, gpu_image, 2).matches, "drawing resumes after arena cap refusal");
        // Non-default zero-valued IDs must not underflow a slot array.
        world.get<render::MeshRenderer>(entity)->mesh = {0, last_mesh->owner};
        expect(renderer.draw(world, camera, *target, {}, gpu_error) && renderer.last_draw_count() == 0,
               "malformed zero-valued owned mesh id is skipped");
        world.get<render::MeshRenderer>(entity)->mesh = *last_mesh;
        world.get<render::MeshRenderer>(entity)->texture = {0, texture->owner};
        expect(renderer.draw(world, camera, *target, {}, gpu_error) && renderer.last_draw_count() == 0,
               "malformed zero-valued owned texture id is skipped");
        world.get<render::MeshRenderer>(entity)->texture = *texture;
        expect(lykoware::write_ppm(gpu_image, output / "gpu-skinning.ppm"), "GPU diagnostic capture is saved");
    }
    std::filesystem::create_directories(output);
    expect(lykoware::write_ppm(rest_image, output / "skin-rest.ppm")
               && lykoware::write_ppm(moved_image, output / "skin-posed.ppm")
               && lykoware::write_ppm(both, output / "skin-independent.ppm"), "diagnostic captures are saved");
    return failures == 0;
}

}

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5) {
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
    std::vector<std::uint32_t> vertex, fragment, skin;
    if (!rhi::read_spirv_file(argv[1], vertex, error) || !rhi::read_spirv_file(argv[2], fragment, error)) {
        std::cerr << error.message << '\n'; return 1;
    }
    if (argc == 5 && !rhi::read_spirv_file(argv[4], skin, error)) {
        std::cerr << error.message << '\n'; return 1;
    }
    bool passed = true;
    const auto baseline_path = std::filesystem::path(argv[3]) / "static-baseline";
    if (argc == 5) {
        auto baseline = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex, fragment, error);
        passed = baseline && exercise(*device, *baseline, baseline_path, false);
    }
    for (int cycle = 0; cycle < (argc == 5 ? 2 : 1); ++cycle) {
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, vertex, fragment, error, skin);
        const bool cycle_passed = renderer && exercise(*device, *renderer, argv[3], argc == 5);
        passed = passed && cycle_passed;
        if (!renderer) { std::cerr << error.message << '\n'; }
    }
    if (argc == 5) {
        for (const char* file : {"skin-rest.ppm", "skin-posed.ppm", "skin-independent.ppm"}) {
            lykoware::Image baseline, enabled;
            const bool loaded = lykoware::read_ppm(baseline_path / file, baseline)
                             && lykoware::read_ppm(std::filesystem::path(argv[3]) / file, enabled);
            expect(loaded && lykoware::compare_images(baseline, enabled, 2).matches,
                   "animation-enabled static fast path matches the original pipeline");
        }
    }
    return passed && failures == 0 && instance->validation_error_count() == 0 ? 0 : 1;
}
