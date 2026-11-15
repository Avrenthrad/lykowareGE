// render.foliage_mesh and render.foliage (ADR 0096): foliage scattered over terrain and drawn as
// one batch.
//
// With no arguments (render.foliage_mesh) the CPU checks run: the scatter is repeatable, has the
// stated density, stands on the drawn ground, keeps off steep ground, and the batch holds each
// plant scaled, turned and placed. With the lit shaders (render.foliage) a batch of plants is
// one draw whose image matches the same plants drawn one entity each, and an empty scene is empty.
//
// Usage: lykoware_render_foliage_test [LIT_VERTEX.spv LIT_FRAGMENT.spv]
// Exit codes: 0 pass, 1 fail, 77 skip (drawing only: no Vulkan loader, driver or suitable
// device). A software GPU without Vulkan skips; it never passes.

#include <lykoware/ecs.hpp>
#include <lykoware/foliage.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/render.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/terrain_mesh.hpp>
#include <lykoware/transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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

bool near(float a, float b, float tolerance = 1e-5F)
{
    return std::abs(a - b) <= tolerance;
}

// The mesh's height above (x, z) in its own space, by finding the triangle over that point.
std::optional<float> mesh_height(const lykoware::MeshData& mesh, float x, float z)
{
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const math::Vec3 a = mesh.positions[mesh.indices[i]];
        const math::Vec3 b = mesh.positions[mesh.indices[i + 1]];
        const math::Vec3 c = mesh.positions[mesh.indices[i + 2]];
        const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::abs(area) < 1e-9F) {
            continue;
        }
        const float u = ((x - a.x) * (c.z - a.z) - (c.x - a.x) * (z - a.z)) / area;
        const float v = ((b.x - a.x) * (z - a.z) - (x - a.x) * (b.z - a.z)) / area;
        if (u >= -1e-4F && v >= -1e-4F && u + v <= 1.F + 1e-4F) {
            return a.y + u * (b.y - a.y) + v * (c.y - a.y);
        }
    }
    return std::nullopt;
}

// A bumpy 8 x 8 field: no two neighbouring cells are flat, so the split of each cell matters.
std::vector<float> bumps(std::uint32_t side)
{
    std::vector<float> heights;
    for (std::uint32_t z = 0; z < side; ++z) {
        for (std::uint32_t x = 0; x < side; ++x) {
            heights.push_back(1.5F * std::sin(static_cast<float>(x) * 1.3F) * std::cos(static_cast<float>(z) * 0.9F)
                              + 0.6F * static_cast<float>((x * 7 + z * 3) % 4));
        }
    }
    return heights;
}


using Instances = std::vector<render::FoliageInstance>;

bool same(const Instances& a, const Instances& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].position.x != b[i].position.x || a[i].position.y != b[i].position.y || a[i].position.z != b[i].position.z
            || a[i].yaw != b[i].yaw || a[i].scale != b[i].scale) {
            return false;
        }
    }
    return true;
}

// A small four-sided pyramid standing on its base: 0.3 m wide, 0.5 m tall, flat-shaded.
lykoware::MeshData pyramid()
{
    lykoware::MeshData mesh;
    const math::Vec3 apex{0.F, 0.5F, 0.F};
    const math::Vec3 corners[4] = {{-0.15F, 0.F, 0.15F}, {0.15F, 0.F, 0.15F}, {0.15F, 0.F, -0.15F}, {-0.15F, 0.F, -0.15F}};
    for (std::size_t i = 0; i < 4; ++i) {
        const math::Vec3 a = corners[i];
        const math::Vec3 b = corners[(i + 1) % 4];
        const math::Vec3 n = math::normalize(math::cross(b - a, apex - a));
        const auto base = static_cast<std::uint32_t>(mesh.positions.size());
        mesh.positions.insert(mesh.positions.end(), {a, b, apex});
        mesh.normals.insert(mesh.normals.end(), {n, n, n});
        mesh.uvs.insert(mesh.uvs.end(), {{0.F, 1.F}, {1.F, 1.F}, {0.5F, 0.F}});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
    }
    return mesh;
}

void scatter_checks()
{
    const std::vector<float> flat(81, 0.F);
    std::string error;
    render::FoliageScatter scatter;
    scatter.ground = {flat, 9, 1.F};
    scatter.density = 2.F;
    scatter.seed = 42;
    Instances first;
    Instances again;
    expect(render::scatter_foliage(scatter, first, error), "a scatter over flat ground works: " + error);
    expect(render::scatter_foliage(scatter, again, error) && same(first, again), "the same seed gives the same plants");
    expect(first.size() == 128, "2 per square metre over 64 square metres is 128 plants (" + std::to_string(first.size()) + ")");
    bool inside = true;
    for (const render::FoliageInstance& plant : first) {
        inside = inside && plant.position.x >= 0.F && plant.position.x < 8.F && plant.position.z >= 0.F && plant.position.z < 8.F
            && plant.position.y == 0.F && plant.yaw >= 0.F && plant.yaw < 6.2831855F && plant.scale >= 0.8F && plant.scale <= 1.2F;
    }
    expect(inside, "every plant is on the patch, on the ground, with a yaw and scale in range");
    scatter.seed = 43;
    Instances other;
    expect(render::scatter_foliage(scatter, other, error) && other.size() == 128 && !same(first, other),
           "another seed gives other plants");
    scatter.seed = 42;
    scatter.density = 2.5F;
    Instances between;
    expect(render::scatter_foliage(scatter, between, error) && between.size() > 140 && between.size() < 180,
           "2.5 per square metre gives about 160 plants (" + std::to_string(between.size()) + ")");

    // On the drawn ground: a bumpy field, every slope allowed.
    const std::vector<float> heights = bumps(8);
    lykoware::MeshData ground;
    render::FoliageScatter bumpy;
    bumpy.ground = {heights, 8, 2.F};
    bumpy.density = 1.F;
    bumpy.max_slope = 1.5707964F;
    Instances standing;
    if (!render::terrain_mesh(bumpy.ground, ground, error) || !render::scatter_foliage(bumpy, standing, error)) {
        expect(false, "the bumpy ground and its scatter build: " + error);
        return;
    }
    float worst = 0.F;
    for (const render::FoliageInstance& plant : standing) {
        const auto drawn = mesh_height(ground, plant.position.x, plant.position.z);
        worst = std::max(worst, drawn ? std::abs(*drawn - plant.position.y) : 100.F);
    }
    expect(standing.size() == 196 && worst <= 1e-3F,
           "every plant stands on the drawn terrain within 1 mm (worst " + std::to_string(worst) + " m)");

    // A far height does not move plants in cells it does not touch.
    std::vector<float> raised = heights;
    raised[63] += 50.F;
    render::FoliageScatter changed = bumpy;
    changed.ground.heights = raised;
    Instances after;
    expect(render::scatter_foliage(changed, after, error) && after.size() == standing.size(), "the raised field scatters");
    bool untouched = after.size() == standing.size();
    for (std::size_t i = 0; untouched && i < after.size(); ++i) {
        const bool corner_cell = standing[i].position.x >= 12.F && standing[i].position.z >= 12.F;
        untouched = corner_cell || (after[i].position.y == standing[i].position.y && after[i].position.x == standing[i].position.x);
    }
    expect(untouched, "raising one corner only moves the plants of that corner's cell");

    // Steep ground: flat for x <= 4, then rising 2 m per metre (63 degrees).
    std::vector<float> cliff;
    for (int z = 0; z < 9; ++z) {
        for (int x = 0; x < 9; ++x) {
            cliff.push_back(x <= 4 ? 0.F : 2.F * static_cast<float>(x - 4));
        }
    }
    render::FoliageScatter sloped;
    sloped.ground = {cliff, 9, 1.F};
    sloped.density = 2.F;
    Instances kept;
    expect(render::scatter_foliage(sloped, kept, error) && kept.size() == 64
               && std::all_of(kept.begin(), kept.end(), [](const render::FoliageInstance& p) { return p.position.x <= 4.F; }),
           "with max_slope 0.7 no plant stands on the 63 degree slope (" + std::to_string(kept.size()) + " kept)");
    sloped.max_slope = 1.5F;
    Instances all;
    expect(render::scatter_foliage(sloped, all, error) && all.size() == 128
               && std::any_of(all.begin(), all.end(), [](const render::FoliageInstance& p) { return p.position.x > 4.F; }),
           "the fixture can tell: with max_slope 1.5 the slope is planted");

    // Refusals leave the output alone.
    Instances out(3);
    const auto refused = [&](const render::FoliageScatter& bad) {
        error.clear();
        return !render::scatter_foliage(bad, out, error) && !error.empty() && out.size() == 3;
    };
    render::FoliageScatter bad = scatter;
    bad.density = 0.F;
    expect(refused(bad), "density 0 is refused");
    bad.density = std::numeric_limits<float>::quiet_NaN();
    expect(refused(bad), "a NaN density is refused");
    bad = scatter;
    bad.density = 1e6F;
    expect(refused(bad), "more than foliage_max_instances plants is refused");
    bad = scatter;
    bad.max_slope = -0.1F;
    expect(refused(bad), "a negative max_slope is refused");
    bad.max_slope = 2.F;
    expect(refused(bad), "a max_slope past pi/2 is refused");
    bad = scatter;
    bad.min_scale = 0.F;
    expect(refused(bad), "a min_scale of 0 is refused");
    bad.min_scale = 2.F;
    bad.max_scale = 1.F;
    expect(refused(bad), "min_scale above max_scale is refused");
    bad = scatter;
    bad.ground.samples_per_side = 8;
    expect(refused(bad), "a ground with the wrong height count is refused");
    bad = scatter;
    bad.ground.spacing = 0.F;
    expect(refused(bad), "a ground with spacing 0 is refused");
}

void batch_checks()
{
    // One triangle with a corner on each axis, turned a quarter turn, doubled and moved.
    lykoware::MeshData corner;
    const math::Vec3 n = math::normalize(math::Vec3{1.F, 1.F, 1.F});
    corner.positions = {{1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}, {0.F, 0.F, 1.F}};
    corner.normals = {n, n, n};
    corner.indices = {0, 1, 2};
    const render::FoliageInstance turned{{1.F, 2.F, 3.F}, 1.5707964F, 2.F};
    lykoware::MeshData batch;
    std::string error;
    expect(render::foliage_batch(corner, std::span(&turned, 1), batch, error), "a one-plant batch builds: " + error);
    const auto is = [&](std::size_t i, math::Vec3 want) {
        return batch.positions.size() == 3 && near(batch.positions[i].x, want.x, 1e-5F) && near(batch.positions[i].y, want.y, 1e-5F)
            && near(batch.positions[i].z, want.z, 1e-5F);
    };
    expect(is(0, {1.F, 2.F, 1.F}) && is(1, {1.F, 4.F, 3.F}) && is(2, {3.F, 2.F, 3.F}),
           "a quarter turn about +Y sends +X to -Z and +Z to +X, scaled then moved");
    expect(batch.normals.size() == 3 && near(batch.normals[0].x, n.x, 1e-5F) && near(batch.normals[0].z, -n.z, 1e-5F),
           "normals turn with the plant and are not scaled");
    expect(batch.uvs.empty(), "a plant without texture coordinates gives a batch without them");

    const lykoware::MeshData plant = pyramid();
    const std::vector<render::FoliageInstance> three = {{{0.F, 0.F, 0.F}}, {{2.F, 0.F, 0.F}, 1.F, 0.5F}, {{4.F, 1.F, 0.F}, 2.F, 1.5F}};
    expect(render::foliage_batch(plant, three, batch, error), "a three-plant batch builds: " + error);
    expect(batch.positions.size() == 36 && batch.normals.size() == 36 && batch.uvs.size() == 36 && batch.indices.size() == 36
               && batch.indices[12] == 12 && batch.indices[35] == 35,
           "each plant adds its vertices and its indices, offset");
    expect(lykoware::valid_mesh(batch, error), "the batch is a mesh the renderer accepts: " + error);
    const lykoware::Bounds box = lykoware::bounds(batch);
    expect(near(box.max.y, 1.75F, 1e-5F) && box.min.x < -0.14F && box.max.x > 4.1F, "the batch spans its plants");

    lykoware::MeshData out;
    out.indices = {7, 7, 7};
    const auto refused = [&](const lykoware::MeshData& mesh, std::span<const render::FoliageInstance> instances) {
        error.clear();
        return !render::foliage_batch(mesh, instances, out, error) && !error.empty() && out.indices.size() == 3;
    };
    expect(refused(plant, {}), "no instances is refused");
    expect(refused(lykoware::MeshData{}, three), "an empty plant is refused");
    std::vector<render::FoliageInstance> bad = three;
    bad[1].scale = 0.F;
    expect(refused(plant, bad), "a scale of 0 is refused");
    bad = three;
    bad[2].position.y = std::numeric_limits<float>::infinity();
    expect(refused(plant, bad), "a position that is not finite is refused");
    bad = three;
    bad[0].yaw = std::numeric_limits<float>::quiet_NaN();
    expect(refused(plant, bad), "a NaN yaw is refused");
    const std::vector<render::FoliageInstance> crowd(render::foliage_max_instances);
    expect(refused(plant, crowd), "a batch past the mesh limits is refused");
}

const rhi::ClearColor sky{0.F, 0.F, 0.F, 1.F};
const render::Camera camera{{0.0, 12.0, 6.0}, {0.0, 0.0, 0.0}};

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const ecs::World& entities, std::string_view name)
{
    rhi::RhiError error;
    lykoware::Image image;
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const bool ok = target && renderer.draw(entities, camera, *target, sky, error) && target->read_back(image, error);
    expect(ok && image.width() == width, std::string(name) + ": renders (" + error.message + ")");
    return image;
}

std::size_t covered(const lykoware::Image& image)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < image.pixels().size(); i += 3) {
        count += (image.pixels()[i] != 0 || image.pixels()[i + 1] != 0 || image.pixels()[i + 2] != 0) ? 1U : 0U;
    }
    return count;
}

// Plants over 8 m of flat ground, drawn once as a batch and once as an entity each.
void drawing(rhi::Device& device, render::Renderer& renderer)
{
    const std::vector<float> flat(81, 0.F);
    render::FoliageScatter scatter;
    scatter.ground = {flat, 9, 1.F};
    scatter.density = 3.F;
    scatter.seed = 7;
    Instances plants;
    lykoware::MeshData batch;
    std::string problem;
    rhi::RhiError error;
    const lykoware::MeshData plant = pyramid();
    if (!render::scatter_foliage(scatter, plants, problem) || !render::foliage_batch(plant, plants, batch, problem)) {
        expect(false, "the plants scatter and batch: " + problem);
        return;
    }
    const auto plant_id = renderer.add_mesh(plant, error);
    const auto batch_id = renderer.add_mesh(batch, error);
    if (!plant_id || !batch_id) {
        expect(false, "the plant and the batch upload: " + error.message);
        return;
    }
    const math::DVec3 origin{-4.0, 0.0, -4.0};
    const math::Vec3 green{0.3F, 0.8F, 0.2F};

    ecs::World batched;
    const ecs::Entity patch = batched.create();
    batched.add<lykoware::Transform>(patch, lykoware::Transform{origin, {}});
    render::MeshRenderer patch_mesh{render::MeshShape::Cube, {1.F, 1.F, 1.F}, green};
    patch_mesh.mesh = *batch_id;
    batched.add<render::MeshRenderer>(patch, patch_mesh);
    const lykoware::Image one = draw(device, renderer, batched, "the batch");
    const render::DrawStats batch_stats = renderer.last_draw_stats();
    expect(plants.size() == 192 && batch_stats.entities == 1 && batch_stats.draws == 1,
           "192 plants are one entity and one draw (" + std::to_string(batch_stats.draws) + " draws)");

    ecs::World separate;
    for (const render::FoliageInstance& instance : plants) {
        const ecs::Entity entity = separate.create();
        const math::DVec3 at{origin.x + instance.position.x, origin.y + instance.position.y, origin.z + instance.position.z};
        separate.add<lykoware::Transform>(entity, lykoware::Transform{at, math::from_axis_angle({0.F, 1.F, 0.F}, instance.yaw)});
        render::MeshRenderer mesh{render::MeshShape::Cube, {instance.scale, instance.scale, instance.scale}, green};
        mesh.mesh = *plant_id;
        separate.add<render::MeshRenderer>(entity, mesh);
    }
    const lykoware::Image many = draw(device, renderer, separate, "one entity per plant");
    expect(renderer.last_draw_stats().draws == 192, "the same plants as entities are 192 draws");

    const std::size_t cover = covered(one);
    std::size_t differing = 0;
    for (std::size_t i = 0; i + 2 < one.pixels().size() && i + 2 < many.pixels().size(); i += 3) {
        const bool differs = std::abs(one.pixels()[i] - many.pixels()[i]) > 2 || std::abs(one.pixels()[i + 1] - many.pixels()[i + 1]) > 2
            || std::abs(one.pixels()[i + 2] - many.pixels()[i + 2]) > 2;
        differing += differs ? 1U : 0U;
    }
    expect(cover > 500, "the plants cover a good part of the image (" + std::to_string(cover) + " pixels)");
    expect(differing * 50 <= cover, "the batch draws what the separate plants draw, within 2% of its pixels ("
                                        + std::to_string(differing) + " of " + std::to_string(cover) + " differ)");

    // Deliberate break: without the batch's draw there is nothing.
    const lykoware::Image empty = draw(device, renderer, ecs::World{}, "no foliage");
    expect(covered(empty) == 0, "with the batch's draw removed, the plants are gone");
}

}

int main(int argc, char* argv[])
{
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: lykoware_render_foliage_test [LIT_VERTEX.spv LIT_FRAGMENT.spv]\n";
        return EXIT_FAILURE;
    }
    if (argc == 1) {
        scatter_checks();
        batch_checks();
        if (g_failures > 0) {
            std::cerr << g_failures << " check(s) failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "render.foliage_mesh: scatter and batch pass\n";
        return EXIT_SUCCESS;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.foliage test";
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
        drawing(*device, *renderer);
    }
    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "render.foliage: a batch is one draw and matches its plants drawn separately\n";
    return EXIT_SUCCESS;
}
