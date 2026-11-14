// render.materials (ADR 0085): metal-roughness shading with a normal map. A cube is drawn under a
// stated light; the centre of its top and front faces must match a CPU copy of the shader's
// formula within 3 of 255 per channel. Deliberate breaks: a smooth surface must differ from a
// rough one, a metal must tint its highlight, and a tilted normal map must move the light. A
// renderer without material shaders, and one whose materials are disabled again, draws exactly
// as before; parts without a material draw as the lit shader does. Pixels are read well inside
// each face, so edge rasterisation does not matter.
//
// Usage: lykoware_render_materials_test LIT.vert LIT.frag MATERIAL.vert MATERIAL.frag
//        MATERIAL_SHADOWED.vert MATERIAL_SHADOWED.frag SHADOW.vert LIT_SHADOWED.vert
//        LIT_SHADOWED.frag SKINNED.vert
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

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr float pi = 3.14159265358979F;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

const rhi::ClearColor sky{0.F, 0.F, 0.F, 1.F};
const render::Camera camera{{0.0, 2.5, 3.0}, {0.0, 0.0, 0.0}};
const math::DVec3 top{0.0, 0.5, 0.0};
const math::DVec3 front{0.0, 0.0, 0.5};

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

using Rgb = std::array<int, 3>;

Rgb rgb_at(const lykoware::Image& image, math::DVec3 point)
{
    const auto xy = pixel_of(point);
    const std::size_t at = (static_cast<std::size_t>(xy.second) * image.width() + xy.first) * 3;
    return {image.pixels()[at], image.pixels()[at + 1], image.pixels()[at + 2]};
}

math::Vec3 unit(math::Vec3 v)
{
    return math::normalize(v);
}

// A CPU copy of material.slang's shade() for a surface with normal n at `point`.
Rgb reference(const render::Lighting& lighting, math::Vec3 albedo, float roughness_in, float metalness, math::Vec3 n,
              math::DVec3 point)
{
    const math::Vec3 l = unit(lighting.direction);
    const math::Vec3 v = unit(math::relative_to(camera.eye, point));
    const float roughness = std::clamp(roughness_in, 0.03F, 1.F);
    const float nl = std::max(math::dot(n, l), 0.F);
    const float nv = std::max(math::dot(n, v), 1e-4F);
    const math::Vec3 h = unit(l + v);
    const float nh = std::max(math::dot(n, h), 0.F);
    const float vh = std::max(math::dot(v, h), 0.F);
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = nh * nh * (a2 - 1.F) + 1.F;
    const float distribution = a2 / (pi * d * d);
    const float k = a * 0.5F;
    const float geometry = (nv / (nv * (1.F - k) + k)) * (nl / (nl * (1.F - k) + k));
    const float c[3] = {albedo.x, albedo.y, albedo.z};
    Rgb out{};
    for (int i = 0; i < 3; ++i) {
        const float f0 = 0.04F + (c[i] - 0.04F) * metalness;
        const float fresnel = f0 + (1.F - f0) * std::pow(1.F - vh, 5.F);
        const float specular = distribution * geometry * fresnel / (4.F * nv * std::max(nl, 1e-4F));
        const float diffuse = (1.F - fresnel) * (1.F - metalness) * c[i];
        const float direct = (diffuse + pi * specular) * nl;
        const float value = c[i] * lighting.ambient + (1.F - lighting.ambient) * direct;
        out[static_cast<std::size_t>(i)] = static_cast<int>(std::lround(std::clamp(value, 0.F, 1.F) * 255.F));
    }
    return out;
}

bool near(const Rgb& got, const Rgb& want, int tolerance)
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(got[i] - want[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

std::string text(const Rgb& c)
{
    return "(" + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", " + std::to_string(c[2]) + ")";
}

struct Scene {
    std::optional<render::Material> material;
    math::Vec3 colour{1.F, 1.F, 1.F};
    bool ground = false;
};

lykoware::Image draw(rhi::Device& device, render::Renderer& renderer, const Scene& scene, rhi::RhiError& error,
                     bool* drew = nullptr)
{
    error = {};
    ecs::World entities;
    const ecs::Entity cube = entities.create();
    entities.add<lykoware::Transform>(cube, lykoware::Transform{});
    render::MeshRenderer mesh{render::MeshShape::Cube, {1.F, 1.F, 1.F}, scene.colour};
    mesh.material = scene.material;
    entities.add<render::MeshRenderer>(cube, mesh);
    if (scene.ground) {
        const ecs::Entity ground = entities.create();
        lykoware::Transform at;
        at.position = {0.0, -0.55, 0.0};
        entities.add<lykoware::Transform>(ground, at);
        render::MeshRenderer slab{render::MeshShape::Cube, {12.F, 0.1F, 12.F}, {1.F, 1.F, 1.F}};
        slab.material = render::Material{};
        entities.add<render::MeshRenderer>(ground, slab);
    }
    auto target = rhi::RenderTarget::create(device, {width, height, true}, error);
    const bool ok = target && renderer.draw(entities, camera, *target, sky, error);
    if (drew != nullptr) {
        *drew = ok;
    }
    lykoware::Image image;
    if (ok && !target->read_back(image, error)) {
        std::cerr << "read failed: " << error.message << '\n';
    }
    return image;
}

lykoware::Image draw_checked(rhi::Device& device, render::Renderer& renderer, const Scene& scene, std::string_view name)
{
    rhi::RhiError error;
    lykoware::Image image = draw(device, renderer, scene, error);
    expect(image.width() == width, std::string(name) + ": renders (" + error.message + ")");
    return image;
}

bool same(const lykoware::Image& a, const lykoware::Image& b, std::uint8_t tolerance)
{
    return a.width() == width && b.width() == width && lykoware::compare_images(a, b, tolerance).matches;
}

// A 1x1 texture of one texel.
std::optional<render::TextureId> texel(render::Renderer& renderer, std::array<std::uint8_t, 4> rgba, rhi::TextureFormat format)
{
    lykoware::RgbaImage image;
    image.width = 1;
    image.height = 1;
    image.pixels = {rgba[0], rgba[1], rgba[2], rgba[3]};
    render::TextureSettings settings;
    settings.format = format;
    settings.mipmaps = false;
    rhi::RhiError error;
    auto id = renderer.add_texture(image, settings, error);
    expect(id.has_value(), "a 1x1 texture uploads: " + error.message);
    return id;
}

// What the shader unpacks from a normal-map texel, tilted by scale, in the top face's frame
// (tangent +x, bitangent -z, normal +y).
math::Vec3 top_normal(std::array<std::uint8_t, 4> rgba, float scale)
{
    const float x = (static_cast<float>(rgba[0]) / 255.F * 2.F - 1.F) * scale;
    const float y = (static_cast<float>(rgba[1]) / 255.F * 2.F - 1.F) * scale;
    const float z = static_cast<float>(rgba[2]) / 255.F * 2.F - 1.F;
    return unit(math::Vec3{x, z, -y});
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
    if (argc != 11) {
        std::cerr << "usage: lykoware_render_materials_test LIT.vert LIT.frag MATERIAL.vert MATERIAL.frag "
                     "MATERIAL_SHADOWED.vert MATERIAL_SHADOWED.frag SHADOW.vert LIT_SHADOWED.vert LIT_SHADOWED.frag "
                     "SKINNED.vert\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware render.materials test";
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
    {
        const auto lit_vertex = words(argv[1]);
        const auto lit_fragment = words(argv[2]);
        const auto material_vertex = words(argv[3]);
        const auto material_fragment = words(argv[4]);
        const auto shadowed_vertex = words(argv[5]);
        const auto shadowed_fragment = words(argv[6]);
        const auto caster_vertex = words(argv[7]);
        const auto receiver_vertex = words(argv[8]);
        const auto receiver_fragment = words(argv[9]);
        const auto skinned_vertex = words(argv[10]);
        if (g_failures != 0) {
            return EXIT_FAILURE;
        }
        const render::MaterialShaders shaders{material_vertex, material_fragment, shadowed_vertex, shadowed_fragment};
        auto renderer = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error);
        if (!renderer) {
            std::cerr << "FAIL: the renderer is created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        const render::Lighting lighting{{0.3F, 1.F, 0.6F}, 0.25F};
        expect(renderer->set_lighting(lighting, error), "the light is accepted");

        // Off: a material on the cube changes nothing until the renderer has material shaders.
        const render::Material rough{1.F, 0.F, 1.F};
        const lykoware::Image plain = draw_checked(*device, *renderer, {}, "plain, off");
        const lykoware::Image ignored = draw_checked(*device, *renderer, {rough}, "material, off");
        expect(same(plain, ignored, 0), "without material shaders a material draws as base colour, byte for byte");
        expect(!renderer->materials_enabled() && renderer->last_material_draw_count() == 0, "materials start off");

        // A failed enable leaves materials off.
        error = {};
        expect(!renderer->enable_materials({{}, material_fragment, shadowed_vertex, shadowed_fragment}, error)
                   && !renderer->materials_enabled(),
               "a missing material vertex shader is refused and materials stay off");

        if (!renderer->enable_materials(shaders, error)) {
            const bool unavailable = error.failure == rhi::RhiFailure::NoSuitableDevice;
            std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
            return unavailable ? exit_skip : EXIT_FAILURE;
        }
        expect(renderer->materials_enabled(), "materials are on");

        // Parts without a material draw as the lit shader does.
        const lykoware::Image plain_on = draw_checked(*device, *renderer, {}, "plain, on");
        expect(same(plain, plain_on, 1), "a part without a material draws as the lit shader, within 1 of 255");
        expect(renderer->last_material_draw_count() == 0, "a plain cube is not counted as a material draw");

        // Numeric checks against the CPU formula.
        struct Case {
            const char* name;
            math::Vec3 colour;
            render::Material material;
        };
        const Case cases[] = {
            {"rough dielectric", {1.F, 1.F, 1.F}, {1.F, 0.F, 1.F}},
            {"half-rough dielectric", {0.9F, 0.5F, 0.2F}, {0.5F, 0.F, 1.F}},
            {"half-rough metal", {0.9F, 0.5F, 0.2F}, {0.5F, 1.F, 1.F}},
            {"satin half metal", {0.4F, 0.7F, 0.9F}, {0.7F, 0.5F, 1.F}},
        };
        for (const Case& c : cases) {
            const lykoware::Image image = draw_checked(*device, *renderer, {c.material, c.colour}, c.name);
            if (image.width() != width) {
                continue;
            }
            expect(renderer->last_material_draw_count() == 1, std::string(c.name) + ": one material draw");
            for (const auto& [point, n, face] : {std::tuple{top, math::Vec3{0.F, 1.F, 0.F}, "top"},
                                                 std::tuple{front, math::Vec3{0.F, 0.F, 1.F}, "front"}}) {
                const Rgb got = rgb_at(image, point);
                const Rgb want = reference(lighting, c.colour, c.material.roughness, c.material.metalness, n, point);
                std::cout << c.name << " " << face << ": " << text(got) << " want " << text(want) << '\n';
                expect(near(got, want, 3), std::string(c.name) + " " + face + " follows the formula within 3 of 255");
            }
        }

        // Deliberate breaks, with the light mirrored about the top face towards the camera.
        const math::Vec3 to_eye = unit(math::relative_to(camera.eye, top));
        const render::Lighting mirror{{to_eye.x * -1.F, to_eye.y, to_eye.z * -1.F}, 0.25F};
        expect(renderer->set_lighting(mirror, error), "the mirrored light is accepted");
        const math::Vec3 red{0.5F, 0.1F, 0.1F};
        const math::Vec3 up{0.F, 1.F, 0.F};
        const Rgb matte = rgb_at(draw_checked(*device, *renderer, {render::Material{1.F, 0.F, 1.F}, red}, "matte"), top);
        const Rgb gloss = rgb_at(draw_checked(*device, *renderer, {render::Material{0.6F, 0.F, 1.F}, red}, "gloss"), top);
        const Rgb metal = rgb_at(draw_checked(*device, *renderer, {render::Material{0.6F, 1.F, 1.F}, red}, "metal"), top);
        std::cout << "highlight: matte " << text(matte) << ", gloss " << text(gloss) << ", metal " << text(metal) << '\n';
        expect(gloss[1] > matte[1] + 15, "a smooth surface shows a highlight a rough one does not");
        expect(near(gloss, reference(mirror, red, 0.6F, 0.F, up, top), 3) && near(metal, reference(mirror, red, 0.6F, 1.F, up, top), 3),
               "the highlights follow the formula");
        expect(std::abs((gloss[1] - matte[1]) - (gloss[0] - matte[0])) <= 3, "a dielectric's highlight is white");
        expect(metal[0] > metal[1] + 100, "a metal tints its highlight with its colour");

        // Normal maps: a texel tilted 45 degrees towards +x, then towards +y (up in the image,
        // which is -z on the top face). Each faces its own light and turns from the other.
        expect(renderer->set_lighting({{1.F, 1.F, 0.F}, 0.1F}, error), "the +x light is accepted");
        const std::array<std::uint8_t, 4> tilt_x{218, 128, 218, 255};
        const std::array<std::uint8_t, 4> tilt_y{128, 218, 218, 255};
        const auto map_x = texel(*renderer, tilt_x, rhi::TextureFormat::Rgba8Unorm);
        const auto map_y = texel(*renderer, tilt_y, rhi::TextureFormat::Rgba8Unorm);
        if (map_x && map_y) {
            const math::Vec3 white{1.F, 1.F, 1.F};
            render::Material tilted{1.F, 0.F, 1.F, *map_x};
            const lykoware::Image flat_image = draw_checked(*device, *renderer, {render::Material{1.F, 0.F, 1.F}, white}, "flat");
            const lykoware::Image tilted_image = draw_checked(*device, *renderer, {tilted, white}, "tilted +x");
            render::Material unscaled = tilted;
            unscaled.normal_scale = 0.F;
            const lykoware::Image unscaled_image = draw_checked(*device, *renderer, {unscaled, white}, "scale 0");
            const render::Lighting x_light{{1.F, 1.F, 0.F}, 0.1F};
            const Rgb flat_top = rgb_at(flat_image, top);
            const Rgb tilted_top = rgb_at(tilted_image, top);
            const Rgb want = reference(x_light, white, 1.F, 0.F, top_normal(tilt_x, 1.F), top);
            std::cout << "normal map +x: flat " << text(flat_top) << ", tilted " << text(tilted_top) << " want " << text(want) << '\n';
            expect(near(tilted_top, want, 3), "a normal map tilted towards +x follows the formula");
            expect(tilted_top[0] > flat_top[0] + 15, "a normal map tilted towards the light brightens the face");
            expect(same(flat_image, unscaled_image, 0), "a normal scale of 0 is the flat surface, byte for byte");

            const render::Lighting toward_up_image{{0.F, 1.F, -1.F}, 0.1F};
            const render::Lighting away{{0.F, 1.F, 1.F}, 0.1F};
            render::Material tilted_y{1.F, 0.F, 1.F, *map_y};
            expect(renderer->set_lighting(toward_up_image, error), "the -z light is accepted");
            const Rgb facing = rgb_at(draw_checked(*device, *renderer, {tilted_y, white}, "tilted +y, -z light"), top);
            expect(renderer->set_lighting(away, error), "the +z light is accepted");
            const Rgb turned = rgb_at(draw_checked(*device, *renderer, {tilted_y, white}, "tilted +y, +z light"), top);
            std::cout << "normal map +y: facing " << text(facing) << ", turned " << text(turned) << '\n';
            expect(near(facing, reference(toward_up_image, white, 1.F, 0.F, top_normal(tilt_y, 1.F), top), 3),
                   "a normal map tilted up the image leans towards -z on the top face");
            expect(facing[0] > turned[0] + 40, "+y in the normal map is up in the image, not down");
        }

        // The metal-roughness texture multiplies the factors: green 0.5 roughness and blue 1
        // metal with factors of 1 draw as factors of 0.5 and 1.
        expect(renderer->set_lighting(lighting, error), "the light is restored");
        const auto mr = texel(*renderer, {0, 128, 255, 255}, rhi::TextureFormat::Rgba8Unorm);
        if (mr) {
            const math::Vec3 gold{0.9F, 0.7F, 0.3F};
            render::Material textured{1.F, 1.F, 1.F};
            textured.metal_roughness = *mr;
            const lykoware::Image by_texture = draw_checked(*device, *renderer, {textured, gold}, "metal-roughness texture");
            const lykoware::Image by_factor =
                draw_checked(*device, *renderer, {render::Material{128.F / 255.F, 1.F, 1.F}, gold}, "metal-roughness factors");
            expect(same(by_texture, by_factor, 1), "the metal-roughness texture multiplies the factors");
        }

        // Refusals: the draw fails with InvalidArgument and draws nothing.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto srgb = texel(*renderer, {218, 128, 218, 255}, rhi::TextureFormat::Rgba8Srgb);
        render::Material srgb_map{1.F, 0.F, 1.F};
        if (srgb) {
            srgb_map.normal_map = *srgb;
        }
        render::Material foreign{1.F, 0.F, 1.F};
        foreign.normal_map = render::TextureId{99, 1};
        const std::pair<render::Material, const char*> bad[] = {
            {{1.5F, 0.F, 1.F}, "a roughness above 1"},
            {{-0.1F, 0.F, 1.F}, "a roughness below 0"},
            {{1.F, nan, 1.F}, "a NaN metalness"},
            {{1.F, 0.F, 2.5F}, "a normal scale above 2"},
            {{1.F, 0.F, -1.F}, "a negative normal scale"},
            {srgb_map, "an sRGB normal map"},
            {foreign, "a texture another renderer made"},
        };
        for (const auto& [material, why] : bad) {
            bool drew = true;
            draw(*device, *renderer, {material}, error, &drew);
            expect(!drew && error.failure == rhi::RhiFailure::InvalidArgument, std::string(why) + " is refused at draw");
        }
        {
            lykoware::MeshData quad;
            quad.positions = {{0.F, 0.F, 0.F}, {1.F, 0.F, 0.F}, {0.F, 1.F, 0.F}};
            quad.normals = {{0.F, 0.F, 1.F}, {0.F, 0.F, 1.F}, {0.F, 0.F, 1.F}};
            quad.uvs = {{0.F, 0.F}, {1.F, 0.F}, {0.F, 1.F}};
            quad.indices = {0, 1, 2};
            render::MeshPart part{0, 3};
            part.material = render::Material{2.F, 0.F, 1.F};
            error = {};
            expect(!renderer->add_mesh(quad, std::span(&part, 1), error) && error.failure == rhi::RhiFailure::InvalidArgument,
                   "a mesh part with a bad material is refused");
            part.material = render::Material{0.5F, 0.5F, 1.F};
            expect(renderer->add_mesh(quad, std::span(&part, 1), error).has_value(), "a mesh part with a good material is accepted");
        }

        // Materials and shadows together: the material shader receives the shadow.
        const render::Lighting slanted{{0.F, 1.F, -1.F}, 0.25F};
        expect(renderer->set_lighting(slanted, error), "the slanted light is accepted");
        render::Shadows shadows;
        shadows.resolution = 1024;
        shadows.half_extent = 4.F;
        shadows.depth_range = 16.F;
        shadows.bias = 0.02F;
        expect(renderer->enable_shadows({caster_vertex, receiver_vertex, receiver_fragment}, shadows, error),
               "shadows are enabled: " + error.message);
        const lykoware::Image shadowed = draw_checked(*device, *renderer, {render::Material{1.F, 0.F, 1.F}, {1.F, 1.F, 1.F}, true}, "shadowed");
        if (shadowed.width() == width) {
            const Rgb under = rgb_at(shadowed, {0.0, -0.5, 0.8});
            const Rgb open = rgb_at(shadowed, {1.5, -0.5, 1.0});
            std::cout << "shadow: under " << text(under) << ", open " << text(open) << '\n';
            expect(near(under, {64, 64, 64}, 2), "ground in the cube's shadow keeps only the ambient share");
            expect(open[0] > under[0] + 40, "open ground is lit");
            expect(renderer->last_material_draw_count() == 2, "the cube and the ground are material draws");
        }
        renderer->disable_shadows();

        // Disabled again: exactly the base-colour image.
        renderer->disable_materials();
        expect(renderer->set_lighting(lighting, error), "the light is restored again");
        const lykoware::Image after = draw_checked(*device, *renderer, {rough}, "material, disabled");
        expect(same(plain, after, 0) && !renderer->materials_enabled() && renderer->last_material_draw_count() == 0,
               "disabling materials draws exactly as before they were enabled");

        // A skinned renderer refuses materials (a later slice).
        auto skinned = render::Renderer::create(*device, rhi::ColorFormat::Rgba8Unorm, lit_vertex, lit_fragment, error, skinned_vertex);
        expect(skinned != nullptr, "the skinned renderer is created");
        if (skinned) {
            error = {};
            expect(!skinned->enable_materials(shaders, error) && error.failure == rhi::RhiFailure::InvalidArgument
                       && !skinned->materials_enabled(),
                   "a skinned renderer refuses materials");
        }
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " material assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    if (instance->validation_error_count() != 0) {
        std::cerr << "FAIL: " << instance->validation_error_count() << " Vulkan validation error(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "render materials passed\n";
    return EXIT_SUCCESS;
}
