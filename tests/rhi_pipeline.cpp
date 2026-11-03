// rhi.pipeline: the first draw. Loads the Slang triangle compiled at build
// time (tests/shaders/triangle.slang via cmake/Shaders.cmake), builds a
// graphics pipeline, draws into an offscreen RenderTarget and checks the
// pixels through the image harness. Exact rasterisation differs between
// GPUs at triangle edges, so the checks sample well inside each region
// rather than comparing a stored image; drawing twice must give identical
// bytes on the same device.
//
// Usage: lykoware_rhi_pipeline_test VERTEX.spv FRAGMENT.spv OUTPUT_DIR
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable
// device). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/image.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

void check_triangle(const lykoware::Image& image)
{
    expect(image.width() == size && image.height() == size, "read-back has the target size");
    if (image.width() != size || image.height() != size) {
        return;
    }
    // Triangle vertices (clip space): top (0,-0.6) red, bottom-right (0.6,0.6)
    // green, bottom-left (-0.6,0.6) blue. Clear colour is black.
    const Pixel corner = at(image, 1, 1);
    const Pixel top = at(image, 32, 14);
    const Pixel right = at(image, 48, 48);
    const Pixel left = at(image, 15, 48);
    const Pixel centre = at(image, 32, 34);
    expect(corner.r == 0 && corner.g == 0 && corner.b == 0, "outside the triangle keeps the clear colour");
    expect(top.r > top.g && top.r > top.b, "near the top vertex is mostly red");
    expect(right.g > right.r && right.g > right.b, "near the bottom-right vertex is mostly green");
    expect(left.b > left.r && left.b > left.g, "near the bottom-left vertex is mostly blue");
    expect(centre.r > 20 && centre.g > 20 && centre.b > 20, "the centre interpolates all three colours");
}

void test_bad_input(const lykoware::rhi::Device& device, const std::filesystem::path& vertex_path)
{
    lykoware::rhi::RhiError error;
    const std::vector<std::uint32_t> not_spirv = {0xDEADBEEFU, 0U};
    expect(lykoware::rhi::Shader::load(device, not_spirv, lykoware::rhi::ShaderStage::Vertex, error) == nullptr
               && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
           "data without the SPIR-V magic number is refused");
    expect(lykoware::rhi::Shader::load_file(device, vertex_path.string() + ".missing",
                                            lykoware::rhi::ShaderStage::Vertex, error)
                   == nullptr
               && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
           "a missing shader file is refused");

    auto vertex = lykoware::rhi::Shader::load_file(device, vertex_path, lykoware::rhi::ShaderStage::Vertex, error);
    lykoware::rhi::GraphicsPipelineDesc swapped;
    swapped.vertex = vertex.get();
    swapped.fragment = vertex.get();
    expect(lykoware::rhi::GraphicsPipeline::create(device, swapped, error) == nullptr
               && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
           "a vertex shader in the fragment slot is refused");
    lykoware::rhi::GraphicsPipelineDesc missing;
    missing.vertex = vertex.get();
    expect(lykoware::rhi::GraphicsPipeline::create(device, missing, error) == nullptr
               && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
           "a pipeline without a fragment shader is refused");
}

}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cerr << "usage: lykoware_rhi_pipeline_test VERTEX.spv FRAGMENT.spv OUTPUT_DIR\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path vertex_path = argv[1];
    const std::filesystem::path fragment_path = argv[2];
    const std::filesystem::path output = argv[3];

    lykoware::rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware rhi.pipeline test";
    instance_config.validation = lykoware::rhi::Validation::IfAvailable;
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create(instance_config, error);
    if (!instance) {
        const bool unavailable = error.failure == lykoware::rhi::RhiFailure::NoLoader
            || error.failure == lykoware::rhi::RhiFailure::NoDriver
            || error.failure == lykoware::rhi::RhiFailure::VersionTooOld;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }
    auto device = lykoware::rhi::Device::create(*instance, {}, error);
    if (!device) {
        const bool unavailable = error.failure == lykoware::rhi::RhiFailure::NoSuitableDevice;
        std::cout << (unavailable ? "SKIP: " : "FAIL: ") << error.message << '\n';
        return unavailable ? exit_skip : EXIT_FAILURE;
    }
    std::cout << "device: " << device->info().name << '\n';

    {
        auto vertex = lykoware::rhi::Shader::load_file(*device, vertex_path, lykoware::rhi::ShaderStage::Vertex, error);
        expect(vertex != nullptr, "the compiled vertex shader loads");
        auto fragment =
            lykoware::rhi::Shader::load_file(*device, fragment_path, lykoware::rhi::ShaderStage::Fragment, error);
        expect(fragment != nullptr, "the compiled fragment shader loads");
        if (!vertex || !fragment) {
            std::cerr << "  " << error.message << '\n';
            return EXIT_FAILURE;
        }

        lykoware::rhi::GraphicsPipelineDesc desc;
        desc.vertex = vertex.get();
        desc.fragment = fragment.get();
        desc.color_format = lykoware::rhi::ColorFormat::Rgba8Unorm;
        auto pipeline = lykoware::rhi::GraphicsPipeline::create(*device, desc, error);
        expect(pipeline != nullptr, "the triangle pipeline is created");
        if (!pipeline) {
            std::cerr << "  " << error.message << '\n';
            return EXIT_FAILURE;
        }
        // Shaders are no longer needed once the pipeline exists.
        vertex.reset();
        fragment.reset();

        auto target = lykoware::rhi::RenderTarget::create(*device, {size, size}, error);
        expect(target != nullptr, "the render target is created");
        if (!target) {
            return EXIT_FAILURE;
        }

        lykoware::Image first;
        expect(target->draw({0.F, 0.F, 0.F, 1.F}, *pipeline, 3, error), "the triangle draws");
        const lykoware::rhi::RenderTargetCapture capture{*target};
        expect(capture.capture(first), "the drawn target reads back through the image harness");
        check_triangle(first);

        lykoware::Image second;
        expect(target->draw({0.F, 0.F, 0.F, 1.F}, *pipeline, 3, error), "the triangle draws again");
        expect(capture.capture(second), "the second draw reads back");
        expect(lykoware::compare_images(first, second, 0).matches, "drawing twice gives identical bytes");

        if (g_failures != 0) {
            std::error_code ignored;
            std::filesystem::create_directories(output, ignored);
            if (lykoware::write_ppm(first, output / "triangle-actual.ppm")) {
                std::cerr << "  wrote " << (output / "triangle-actual.ppm").string() << '\n';
            }
        }

        // A pipeline built for another colour format cannot draw into this target.
        auto vertex_again = lykoware::rhi::Shader::load_file(*device, vertex_path, lykoware::rhi::ShaderStage::Vertex, error);
        auto fragment_again =
            lykoware::rhi::Shader::load_file(*device, fragment_path, lykoware::rhi::ShaderStage::Fragment, error);
        lykoware::rhi::GraphicsPipelineDesc bgra_desc;
        bgra_desc.vertex = vertex_again.get();
        bgra_desc.fragment = fragment_again.get();
        bgra_desc.color_format = lykoware::rhi::ColorFormat::Bgra8Unorm;
        auto bgra = lykoware::rhi::GraphicsPipeline::create(*device, bgra_desc, error);
        expect(bgra != nullptr, "a BGRA pipeline is created");
        if (bgra) {
            expect(!target->draw({0.F, 0.F, 0.F, 1.F}, *bgra, 3, error)
                       && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
                   "drawing with a pipeline of another colour format is refused");
        }

        test_bad_input(*device, vertex_path);
    }

    device.reset();
    if (instance->validation_enabled()) {
        expect(instance->validation_error_count() == 0, "loading shaders, building and drawing produce no validation errors");
    } else {
        std::cout << "note: validation layer not installed; validation not checked\n";
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " RHI pipeline assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "rhi pipeline passed\n";
    return EXIT_SUCCESS;
}
