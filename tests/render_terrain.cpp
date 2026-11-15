// render.terrain_mesh and render.terrain (ADR 0095): terrain drawn from a grid of heights.
//
// With no arguments (render.terrain_mesh) the CPU checks run: layout, normals, the coarse step,
// refusals, and that the mesh is the ground physics::HeightFieldShape collides with, including
// which way each cell is split. With the lit shaders (render.terrain) the mesh is drawn: flat
// ground and a ramp must match the lit formula within 2 of 255, an empty scene must not, and a
// cube under the ground must stay hidden.
//
// Usage: lykoware_render_terrain_test [LIT_VERTEX.spv LIT_FRAGMENT.spv]
// Exit codes: 0 pass, 1 fail, 77 skip (drawing only: no Vulkan loader, driver or suitable
// device). A software GPU without Vulkan skips; it never passes.

#include <lykoware/ecs.hpp>
#include <lykoware/image.hpp>
#include <lykoware/math.hpp>
#include <lykoware/mesh.hpp>
#include <lykoware/physics.hpp>
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
namespace physics = lykoware::physics;
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

void layout_and_normals()
{
    const std::vector<float> heights = {0.F, 1.F, 2.F, 3.F, 4.F, 5.F, 6.F, 7.F, 8.F, 9.F, 10.F, 11.F, 12.F, 13.F, 14.F, 15.F};
    lykoware::MeshData mesh;
    std::string error;
    expect(render::terrain_mesh({heights, 4, 2.F, 1, 4.F}, mesh, error), "a 4 x 4 patch builds: " + error);
    expect(mesh.positions.size() == 16 && mesh.normals.size() == 16 && mesh.uvs.size() == 16 && mesh.indices.size() == 54,
           "one vertex per sample and two triangles per cell");
    bool placed = mesh.positions.size() == 16 && mesh.uvs.size() == 16;
    for (std::size_t z = 0; placed && z < 4; ++z) {
        for (std::size_t x = 0; x < 4; ++x) {
            const math::Vec3 p = mesh.positions[z * 4 + x];
            const math::Vec2 uv = mesh.uvs[z * 4 + x];
            placed = placed && near(p.x, static_cast<float>(x) * 2.F) && near(p.y, heights[z * 4 + x])
                && near(p.z, static_cast<float>(z) * 2.F) && near(uv.x, p.x / 4.F) && near(uv.y, p.z / 4.F);
        }
    }
    expect(placed, "sample (x, z) sits at {x * spacing, height, z * spacing} with tiling texture coordinates");
    expect(lykoware::valid_mesh(mesh, error), "the mesh is one the renderer accepts: " + error);
    bool up = true;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const math::Vec3 a = mesh.positions[mesh.indices[i]];
        const math::Vec3 b = mesh.positions[mesh.indices[i + 1]];
        const math::Vec3 c = mesh.positions[mesh.indices[i + 2]];
        up = up && math::cross(b - a, c - a).y > 0.F;
    }
    expect(up, "every triangle faces up (counter-clockwise from above)");

    // This field is the plane h = x + 4 z in samples, so 0.5 x + 2 z in metres: one normal everywhere.
    const math::Vec3 plane = math::normalize(math::Vec3{-0.5F, 1.F, -2.F});
    const bool flat = std::all_of(mesh.normals.begin(), mesh.normals.end(), [&](math::Vec3 n) {
        return near(n.x, plane.x, 1e-4F) && near(n.y, plane.y, 1e-4F) && near(n.z, plane.z, 1e-4F);
    });
    expect(flat, "a plane's normals are the plane's normal, at the edges too");
}

void coarse_step()
{
    const std::vector<float> heights = bumps(9);
    lykoware::MeshData fine;
    lykoware::MeshData coarse;
    std::string error;
    expect(render::terrain_mesh({heights, 9, 1.F, 1, 1.F}, fine, error), "the fine patch builds: " + error);
    expect(render::terrain_mesh({heights, 9, 1.F, 4, 1.F}, coarse, error), "the step-4 patch builds: " + error);
    expect(fine.positions.size() == 81 && coarse.positions.size() == 9 && coarse.indices.size() == 24,
           "step 4 over 9 samples keeps 3 per side: 9 vertices, 8 triangles");
    bool same = coarse.positions.size() == 9;
    for (std::size_t j = 0; same && j < 3; ++j) {
        for (std::size_t i = 0; i < 3; ++i) {
            const math::Vec3 p = coarse.positions[j * 3 + i];
            same = same && near(p.x, static_cast<float>(i * 4)) && near(p.z, static_cast<float>(j * 4))
                && near(p.y, heights[j * 4 * 9 + i * 4]);
        }
    }
    expect(same, "the coarse mesh's vertices are the fine field's samples, over the same ground");
    expect(lykoware::valid_mesh(coarse, error), "the coarse mesh is valid: " + error);
}

void refusals()
{
    const std::vector<float> heights(81, 1.F);
    lykoware::MeshData mesh;
    mesh.indices = {7, 7, 7};
    std::string error;
    const auto refused = [&](const render::TerrainPatch& patch) {
        error.clear();
        return !render::terrain_mesh(patch, mesh, error) && !error.empty() && mesh.indices.size() == 3;
    };
    expect(refused({std::span(heights).first(1), 1, 1.F, 1, 1.F}), "one sample per side is refused");
    expect(refused({std::span(heights).first(80), 9, 1.F, 1, 1.F}), "a wrong height count is refused");
    expect(refused({heights, 9, 1.F, 0, 1.F}), "step 0 is refused");
    expect(refused({heights, 9, 1.F, 3, 1.F}), "a step that does not divide samples - 1 is refused");
    expect(refused({heights, 9, 0.F, 1, 1.F}), "spacing 0 is refused");
    expect(refused({heights, 9, std::numeric_limits<float>::infinity(), 1, 1.F}), "an infinite spacing is refused");
    expect(refused({heights, 9, 1.F, 1, -1.F}), "a negative texture_metres is refused");
    std::vector<float> broken = heights;
    broken[40] = std::numeric_limits<float>::quiet_NaN();
    expect(refused({broken, 9, 1.F, 1, 1.F}), "a NaN height is refused");
    const std::vector<float> huge(std::size_t{4096} * 4096, 0.F);
    expect(refused({huge, 4096, 1.F, 1, 1.F}), "a mesh past the vertex limit is refused");
    expect(render::terrain_mesh({huge, 4096, 1.F, 5, 1.F}, mesh, error) && mesh.positions.size() == std::size_t{820} * 820,
           "the same field with a larger step builds: " + error);
}

// The drawn ground is the ground bodies stand on: rays cast down onto a physics height field,
// placed away from the origin, land on the mesh.
void matches_collision()
{
    constexpr std::uint32_t side = 8;
    constexpr float spacing = 2.F;
    const std::vector<float> heights = bumps(side);
    lykoware::MeshData mesh;
    std::string error;
    if (!render::terrain_mesh({heights, side, spacing, 1, 1.F}, mesh, error)) {
        expect(false, "the patch builds: " + error);
        return;
    }
    auto world = physics::World::create({}, error);
    if (!world) {
        expect(false, "the physics world is created: " + error);
        return;
    }
    physics::BodyDesc ground;
    ground.shape = physics::HeightFieldShape{heights, side, spacing};
    ground.motion = physics::MotionType::Static;
    ground.position = {100.0, 3.0, -40.0};
    if (!world->create_body(ground, error)) {
        expect(false, "the height field body is created: " + error);
        return;
    }
    // The other way to split a cell: along the diagonal from (x + 1, z) to (x, z + 1).
    const auto other_split = [&](float x, float z) {
        const float fx = x / spacing;
        const float fz = z / spacing;
        const auto cx = static_cast<std::size_t>(fx);
        const auto cz = static_cast<std::size_t>(fz);
        const float u = fx - static_cast<float>(cx);
        const float v = fz - static_cast<float>(cz);
        const float h00 = heights[cz * side + cx];
        const float h10 = heights[cz * side + cx + 1];
        const float h01 = heights[(cz + 1) * side + cx];
        const float h11 = heights[(cz + 1) * side + cx + 1];
        return u + v <= 1.F ? h00 + u * (h10 - h00) + v * (h01 - h00) : h11 + (1.F - u) * (h01 - h11) + (1.F - v) * (h10 - h11);
    };
    float worst = 0.F;
    float worst_other = 0.F;
    int hits = 0;
    for (int j = 0; j < 28; ++j) {
        for (int i = 0; i < 28; ++i) {
            const float x = 0.25F + static_cast<float>(i) * 0.5F;
            const float z = 0.25F + static_cast<float>(j) * 0.5F;
            const auto hit = world->cast_ray({ground.position.x + x, ground.position.y + 20.0, ground.position.z + z},
                                             {0.F, -40.F, 0.F});
            const auto drawn = mesh_height(mesh, x, z);
            if (!hit || !drawn) {
                continue;
            }
            ++hits;
            const auto collided = static_cast<float>(hit->point.y - ground.position.y);
            worst = std::max(worst, std::abs(collided - *drawn));
            worst_other = std::max(worst_other, std::abs(collided - other_split(x, z)));
        }
    }
    expect(hits == 28 * 28, "every ray lands on the field and over the mesh (" + std::to_string(hits) + " of 784)");
    // The height field stores heights to within 1 cm.
    expect(worst <= 0.03F, "the mesh is within 3 cm of the collision surface everywhere (worst " + std::to_string(worst) + " m)");
    expect(worst_other > 0.2F, "the fixture can tell: cells split the other way miss the collision surface by "
                                   + std::to_string(worst_other) + " m");
}

const rhi::ClearColor sky{0.F, 0.F, 0.F, 1.F};
const render::Camera camera{{0.0, 12.0, 6.0}, {0.0, 0.0, 0.0}};

std::pair<std::uint32_t, std::uint32_t> pixel_of(math::DVec3 point)
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

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
};

Pixel at(const lykoware::Image& image, math::DVec3 point)
{
    const auto xy = pixel_of(point);
    const std::size_t i = (static_cast<std::size_t>(xy.second) * image.width() + xy.first) * 3;
    if (i + 2 >= image.pixels().size()) {
        return {};
    }
    return {image.pixels()[i], image.pixels()[i + 1], image.pixels()[i + 2]};
}

// What the lit shader gives a white surface with normal n under the default light.
int expected(math::Vec3 n)
{
    const render::Lighting lighting;
    const float diffuse = std::max(0.F, math::dot(math::normalize(n), math::normalize(lighting.direction)));
    return static_cast<int>(std::lround((lighting.ambient + (1.F - lighting.ambient) * diffuse) * 255.F));
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

std::size_t covered(const lykoware::Image& image)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i + 2 < image.pixels().size(); i += 3) {
        count += (image.pixels()[i] != 0 || image.pixels()[i + 1] != 0 || image.pixels()[i + 2] != 0) ? 1U : 0U;
    }
    return count;
}

// A 9 x 9 patch, 1 m apart, centred on the origin: flat for x <= 4, then a ramp rising 0.5 m per
// metre. Flat ground is read at world (-2, 0, 0) and the ramp at (2.5, 1.25, 0).
void drawing(rhi::Device& device, render::Renderer& renderer)
{
    std::vector<float> heights;
    for (int z = 0; z < 9; ++z) {
        for (int x = 0; x < 9; ++x) {
            heights.push_back(x <= 4 ? 0.F : 0.5F * static_cast<float>(x - 4));
        }
    }
    lykoware::MeshData fine;
    lykoware::MeshData coarse;
    std::string problem;
    rhi::RhiError error;
    if (!render::terrain_mesh({heights, 9, 1.F, 1, 1.F}, fine, problem)
        || !render::terrain_mesh({heights, 9, 1.F, 4, 1.F}, coarse, problem)) {
        expect(false, "the patches build: " + problem);
        return;
    }
    const auto fine_id = renderer.add_mesh(fine, error);
    const auto coarse_id = renderer.add_mesh(coarse, error);
    if (!fine_id || !coarse_id) {
        expect(false, "the terrain meshes upload: " + error.message);
        return;
    }
    const lykoware::Transform placed{{-4.0, 0.0, -4.0}, {}};
    const auto scene = [&](std::optional<render::MeshId> terrain, bool cube_below) {
        ecs::World entities;
        if (terrain) {
            const ecs::Entity ground = entities.create();
            entities.add<lykoware::Transform>(ground, placed);
            render::MeshRenderer mesh{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 1.F, 1.F}};
            mesh.mesh = *terrain;
            entities.add<render::MeshRenderer>(ground, mesh);
        }
        if (cube_below) {
            const ecs::Entity cube = entities.create();
            entities.add<lykoware::Transform>(cube, lykoware::Transform{{-2.0, -1.0, 0.0}, {}});
            entities.add<render::MeshRenderer>(cube, render::MeshRenderer{render::MeshShape::Cube, {1.F, 1.F, 1.F}, {1.F, 0.F, 0.F}});
        }
        return entities;
    };
    const math::DVec3 flat_point{-2.0, 0.0, 0.0};
    const math::DVec3 ramp_point{2.5, 1.25, 0.0};
    const int flat_expected = expected({0.F, 1.F, 0.F});
    const int ramp_expected = expected({-0.5F, 1.F, 0.F});
    expect(std::abs(flat_expected - ramp_expected) > 10, "the fixture's flat ground and ramp are lit differently");

    const lykoware::Image image = draw(device, renderer, scene(fine_id, false), "terrain");
    const Pixel flat = at(image, flat_point);
    const Pixel ramp = at(image, ramp_point);
    expect(std::abs(flat.r - flat_expected) <= 2 && flat.g == flat.r && flat.b == flat.r,
           "flat ground is lit as a surface facing up (" + std::to_string(flat.r) + " vs " + std::to_string(flat_expected) + ")");
    expect(std::abs(ramp.r - ramp_expected) <= 2,
           "the ramp is lit by its slope (" + std::to_string(ramp.r) + " vs " + std::to_string(ramp_expected) + ")");

    // Deliberate break: without the terrain's draw the same pixels show the sky.
    const lykoware::Image empty = draw(device, renderer, scene(std::nullopt, false), "no terrain");
    expect(at(empty, flat_point).r == 0 && at(empty, ramp_point).r == 0 && covered(empty) == 0,
           "with the terrain's draw removed, the ground is gone");

    // The ground hides what is under it, and nothing else changes.
    const lykoware::Image buried = draw(device, renderer, scene(fine_id, true), "terrain over a cube");
    const Pixel over = at(buried, flat_point);
    expect(over.r == flat.r && over.g == flat.g && over.b == flat.b, "a cube under the ground stays hidden");
    const lykoware::Image bare = draw(device, renderer, scene(std::nullopt, true), "the cube alone");
    const Pixel cube = at(bare, {-2.0, -0.5, 0.0});
    expect(cube.r > 100 && cube.g == 0, "and that cube shows once the ground is gone");

    // The coarse patch covers the same ground with 8 triangles instead of 128.
    const lykoware::Image far = draw(device, renderer, scene(coarse_id, false), "coarse terrain");
    const std::size_t fine_cover = covered(image);
    const std::size_t coarse_cover = covered(far);
    const std::size_t difference = fine_cover > coarse_cover ? fine_cover - coarse_cover : coarse_cover - fine_cover;
    expect(fine_cover > 1000 && difference * 100 <= fine_cover,
           "the coarse patch covers the same pixels within 1% (" + std::to_string(coarse_cover) + " vs "
               + std::to_string(fine_cover) + ")");
    // Its normals come from samples 4 m apart, so by the crease the ramp is shaded between the two.
    const int far_ramp = at(far, ramp_point).r;
    expect(far_ramp >= std::min(flat_expected, ramp_expected) - 2 && far_ramp <= std::max(flat_expected, ramp_expected) + 2,
           "and its ramp is lit between flat ground and the fine ramp (" + std::to_string(far_ramp) + ")");
}

}

int main(int argc, char* argv[])
{
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: lykoware_render_terrain_test [LIT_VERTEX.spv LIT_FRAGMENT.spv]\n";
        return EXIT_FAILURE;
    }
    if (argc == 1) {
        layout_and_normals();
        coarse_step();
        refusals();
        matches_collision();
        if (g_failures > 0) {
            std::cerr << g_failures << " check(s) failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "render.terrain_mesh: layout, normals, step, refusals and collision agreement pass\n";
        return EXIT_SUCCESS;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.terrain test";
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
    std::cout << "render.terrain: flat ground, a ramp, occlusion and the coarse patch pass\n";
    return EXIT_SUCCESS;
}
