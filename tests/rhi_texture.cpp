// rhi.texture: sampled textures in the RHI (ADR 0037). Draws an unlit, screen-filling quad
// (tests/shaders/textured.slang) into small offscreen targets, so each pixel shows exactly what
// the texture gives at that point.
//
// Usage: lykoware_rhi_texture_test TEXTURED_VERT TEXTURED_FRAG MESH_VERT MESH_FRAG
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable device).

#include <lykoware/image.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/rhi/texture.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace rhi = lykoware::rhi;

constexpr int exit_skip = 77;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == rhi::position_normal_uv_stride);

struct Rgb {
    int r;
    int g;
    int b;
};

Rgb at(const lykoware::Image& image, std::uint32_t x, std::uint32_t y)
{
    const auto pixels = image.pixels();
    const std::size_t i = (static_cast<std::size_t>(y) * image.width() + x) * 3;
    return {pixels[i], pixels[i + 1], pixels[i + 2]};
}

std::vector<std::byte> rgba(const std::vector<std::array<int, 4>>& texels)
{
    std::vector<std::byte> out;
    for (const auto& t : texels) {
        for (const int c : t) {
            out.push_back(static_cast<std::byte>(c));
        }
    }
    return out;
}

// What a Rgba8Unorm target stores for a texel of sRGB value `v`: decoded to linear, then
// written as is.
int srgb_decoded(int v)
{
    const double c = v / 255.0;
    const double linear = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    return static_cast<int>(std::lround(linear * 255.0));
}

struct Kit {
    rhi::Device& device;
    rhi::GraphicsPipeline& pipeline;
};

// A quad over the whole target, with texture coordinates from (0, 0) at the top-left to
// (u_max, 1) at the bottom-right. Returns the read-back image, or an empty one on failure.
lykoware::Image draw(const Kit& kit, const rhi::Texture& texture, std::uint32_t width, std::uint32_t height,
                     float u_max = 1.F)
{
    const Vertex vertices[4] = {{{-1.F, -1.F, 0.5F}, {0.F, 0.F, 1.F}, {0.F, 0.F}},
                                {{1.F, -1.F, 0.5F}, {0.F, 0.F, 1.F}, {u_max, 0.F}},
                                {{1.F, 1.F, 0.5F}, {0.F, 0.F, 1.F}, {u_max, 1.F}},
                                {{-1.F, 1.F, 0.5F}, {0.F, 0.F, 1.F}, {0.F, 1.F}}};
    const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    rhi::RhiError error;
    lykoware::Image image;
    auto vb = rhi::Buffer::create(kit.device, rhi::BufferUsage::Vertex, std::as_bytes(std::span(vertices)), error);
    auto ib = vb ? rhi::Buffer::create(kit.device, rhi::BufferUsage::Index, std::as_bytes(std::span(indices)), error) : nullptr;
    auto target = ib ? rhi::RenderTarget::create(kit.device, {width, height, false}, error) : nullptr;
    rhi::MeshDraw quad;
    quad.vertices = vb.get();
    quad.indices = ib.get();
    quad.texture = &texture;
    const rhi::MeshDraw draws[1] = {quad};
    if (!target || !target->draw_meshes({0.F, 0.F, 0.F, 1.F}, kit.pipeline, draws, error)
        || !target->read_back(image, error)) {
        std::cerr << "  draw: " << error.message << '\n';
        image = {};
    }
    return image;
}

std::unique_ptr<rhi::Texture> texture(rhi::Device& device, rhi::TextureDesc desc, const std::vector<std::byte>& data)
{
    rhi::RhiError error;
    auto t = rhi::Texture::create(device, desc, data, error);
    if (!t) {
        std::cerr << "  texture: " << error.message << '\n';
    }
    return t;
}

void test_exact_texels(const Kit& kit)
{
    // 4x4 distinct texels, Unorm, nearest, one level: each pixel shows its texel exactly.
    std::vector<std::array<int, 4>> texels;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            texels.push_back({x * 60 + 10, y * 60 + 20, 200 - x * 10 - y * 30, 255});
        }
    }
    const auto t = texture(kit.device, {4, 4, rhi::TextureFormat::Rgba8Unorm, false, rhi::TextureWrap::ClampToEdge, rhi::TextureFilter::Nearest},
                           rgba(texels));
    expect(t && t->mip_levels() == 1 && t->width() == 4 && t->height() == 4, "a 4x4 texture is created");
    if (!t) {
        return;
    }
    const lykoware::Image image = draw(kit, *t, 4, 4);
    bool exact = image.width() == 4;
    for (std::uint32_t y = 0; exact && y < 4; ++y) {
        for (std::uint32_t x = 0; exact && x < 4; ++x) {
            const Rgb p = at(image, x, y);
            const auto& want = texels[y * 4 + x];
            exact = p.r == want[0] && p.g == want[1] && p.b == want[2];
        }
    }
    expect(exact, "each pixel shows its texel exactly, rows from the top");
}

void test_srgb(const Kit& kit)
{
    const auto t = texture(kit.device, {4, 1, rhi::TextureFormat::Rgba8Srgb, false, rhi::TextureWrap::ClampToEdge, rhi::TextureFilter::Nearest},
                           rgba({{0, 0, 0, 255}, {64, 64, 64, 255}, {128, 128, 128, 255}, {255, 255, 255, 255}}));
    if (!t) {
        expect(false, "an sRGB texture is created");
        return;
    }
    const lykoware::Image image = draw(kit, *t, 4, 1);
    bool decoded = image.width() == 4;
    const int values[4] = {0, 64, 128, 255};
    for (std::uint32_t x = 0; decoded && x < 4; ++x) {
        decoded = std::abs(at(image, x, 0).r - srgb_decoded(values[x])) <= 1;
    }
    expect(decoded, "an sRGB texture is read as linear light (sRGB 128 -> 55)");
}

void test_mipmaps(const Kit& kit)
{
    // A 64x64 checkerboard of single black and white texels, drawn into 4x4 pixels: 16 texels
    // per pixel, so the GPU picks mip level 4. Nearest filtering and nearest level make each
    // pixel one texel of that level.
    std::vector<std::array<int, 4>> checker;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const int v = (x + y) % 2 == 0 ? 0 : 255;
            checker.push_back({v, v, v, 255});
        }
    }
    const auto data = rgba(checker);
    rhi::TextureDesc desc{64, 64, rhi::TextureFormat::Rgba8Srgb, true, rhi::TextureWrap::Repeat, rhi::TextureFilter::Nearest};
    const auto mipped = texture(kit.device, desc, data);
    expect(mipped && mipped->mip_levels() == 7, "a 64x64 texture has 7 mip levels");
    desc.mipmaps = false;
    const auto flat = texture(kit.device, desc, data);
    if (!mipped || !flat) {
        return;
    }
    // Averaged in linear light, half black and half white is 0.5, which the Unorm target stores
    // as 128. Averaging the sRGB values instead would give sRGB 128, read back as 55.
    const lykoware::Image far = draw(kit, *mipped, 4, 4);
    bool grey = far.width() == 4;
    for (std::uint32_t y = 0; grey && y < 4; ++y) {
        for (std::uint32_t x = 0; grey && x < 4; ++x) {
            grey = std::abs(at(far, x, y).r - 128) <= 2;
        }
    }
    expect(grey, "a far checkerboard averages to half brightness in linear light (" + std::to_string(far.width() ? at(far, 0, 0).r : -1) + ")");
    const lykoware::Image aliased = draw(kit, *flat, 4, 4);
    bool black_or_white = aliased.width() == 4;
    for (std::uint32_t y = 0; black_or_white && y < 4; ++y) {
        for (std::uint32_t x = 0; black_or_white && x < 4; ++x) {
            const int r = at(aliased, x, y).r;
            black_or_white = r == 0 || r == 255;
        }
    }
    expect(black_or_white, "without mipmaps the same view aliases to black or white");
    expect(rhi::full_mip_levels(64, 64) == 7 && rhi::full_mip_levels(5, 3) == 3 && rhi::full_mip_levels(1, 1) == 1,
           "full mip chains go down to 1x1");
}

void test_wrap(const Kit& kit)
{
    // Red then blue, sampled at u = 0.25, 0.75, 1.25, 1.75.
    const auto data = rgba({{255, 0, 0, 255}, {0, 0, 255, 255}});
    struct Case {
        rhi::TextureWrap wrap;
        const char* expected; // r or b per pixel
        const char* name;
    };
    for (const Case c : {Case{rhi::TextureWrap::Repeat, "rbrb", "Repeat"}, Case{rhi::TextureWrap::ClampToEdge, "rbbb", "ClampToEdge"},
                         Case{rhi::TextureWrap::MirroredRepeat, "rbbr", "MirroredRepeat"}}) {
        const auto t = texture(kit.device, {2, 1, rhi::TextureFormat::Rgba8Unorm, false, c.wrap, rhi::TextureFilter::Nearest}, data);
        if (!t) {
            expect(false, std::string{c.name} + " texture is created");
            continue;
        }
        const lykoware::Image image = draw(kit, *t, 4, 1, 2.F);
        std::string got;
        for (std::uint32_t x = 0; image.width() == 4 && x < 4; ++x) {
            const Rgb p = at(image, x, 0);
            got += p.r == 255 && p.b == 0 ? 'r' : p.b == 255 && p.r == 0 ? 'b' : '?';
        }
        expect(got == c.expected, std::string{c.name} + " wraps as " + c.expected + " (got " + got + ")");
    }
}

rhi::TextureDesc sized(std::uint32_t width, std::uint32_t height)
{
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.mipmaps = false;
    return desc;
}

void test_misuse(rhi::Device& device, rhi::GraphicsPipeline& textured, rhi::GraphicsPipeline& untextured)
{
    rhi::RhiError error;
    const std::vector<std::byte> one(4, std::byte{255});
    expect(!rhi::Texture::create(device, sized(0, 1), {}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a zero-width texture is refused");
    expect(!rhi::Texture::create(device, sized(2, 2), one, error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "texture data of the wrong size is refused");
    expect(!rhi::Texture::create(device, sized(1U << 20U, 1), {}, error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a texture wider than the device allows is refused");

    const Vertex vertices[3] = {{{-1.F, -1.F, 0.5F}, {0.F, 0.F, 1.F}, {0.F, 0.F}},
                                {{1.F, -1.F, 0.5F}, {0.F, 0.F, 1.F}, {1.F, 0.F}},
                                {{1.F, 1.F, 0.5F}, {0.F, 0.F, 1.F}, {1.F, 1.F}}};
    const std::uint32_t indices[3] = {0, 1, 2};
    auto vb = rhi::Buffer::create(device, rhi::BufferUsage::Vertex, std::as_bytes(std::span(vertices)), error);
    auto ib = rhi::Buffer::create(device, rhi::BufferUsage::Index, std::as_bytes(std::span(indices)), error);
    auto target = rhi::RenderTarget::create(device, {4, 4, false}, error);
    auto t = rhi::Texture::create(device, sized(1, 1), one, error);
    if (!vb || !ib || !target || !t) {
        expect(false, "misuse fixtures are created: " + error.message);
        return;
    }
    rhi::MeshDraw bare;
    bare.vertices = vb.get();
    bare.indices = ib.get();
    const rhi::MeshDraw no_texture[1] = {bare};
    expect(!target->draw_meshes({}, textured, no_texture, error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a textured pipeline refuses a mesh without a texture");
    bare.texture = t.get();
    const rhi::MeshDraw stray_texture[1] = {bare};
    expect(!target->draw_meshes({}, untextured, stray_texture, error) && error.failure == rhi::RhiFailure::InvalidArgument,
           "a pipeline without a texture refuses a mesh that names one");
}

}

int main(int argc, char* argv[])
{
    if (argc != 5) {
        std::cerr << "usage: lykoware_rhi_texture_test TEXTURED_VERT TEXTURED_FRAG MESH_VERT MESH_FRAG\n";
        return EXIT_FAILURE;
    }
    rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware rhi.texture test";
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
    std::cout << "device: " << device->info().name << '\n';
    {
        auto vertex = rhi::Shader::load_file(*device, argv[1], rhi::ShaderStage::Vertex, error);
        auto fragment = vertex ? rhi::Shader::load_file(*device, argv[2], rhi::ShaderStage::Fragment, error) : nullptr;
        auto mesh_vertex = fragment ? rhi::Shader::load_file(*device, argv[3], rhi::ShaderStage::Vertex, error) : nullptr;
        auto mesh_fragment = mesh_vertex ? rhi::Shader::load_file(*device, argv[4], rhi::ShaderStage::Fragment, error) : nullptr;
        if (!mesh_fragment) {
            std::cerr << "FAIL: the shaders load: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.vertex_layout = rhi::VertexLayout::PositionNormalUv;
        desc.texture = true;
        auto textured = rhi::GraphicsPipeline::create(*device, desc, error);
        rhi::GraphicsPipelineDesc plain;
        plain.vertex = mesh_vertex.get();
        plain.fragment = mesh_fragment.get();
        plain.vertex_layout = rhi::VertexLayout::PositionNormal;
        plain.push_constant_bytes = rhi::push_constant_max_bytes;
        auto untextured = textured ? rhi::GraphicsPipeline::create(*device, plain, error) : nullptr;
        if (!untextured) {
            std::cerr << "FAIL: the pipelines are created: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        expect(textured->texture() && !untextured->texture(), "pipelines report whether they read a texture");
        const Kit kit{*device, *textured};
        test_exact_texels(kit);
        test_srgb(kit);
        test_mipmaps(kit);
        test_wrap(kit);
        test_misuse(*device, *textured, *untextured);
    }
    if (instance->validation_error_count() != 0) {
        expect(false, std::to_string(instance->validation_error_count()) + " Vulkan validation errors");
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " texture assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "rhi texture passed\n";
    return EXIT_SUCCESS;
}
