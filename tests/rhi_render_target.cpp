// rhi.render_target: headless Vulkan rendering read back through the image
// harness. A dynamic-rendering clear must produce exactly the expected solid
// image. Expected images are built here from the clear colour, not stored as
// files: a solid colour needs no fixture. On a mismatch the actual image and
// the harness diff are written to the output directory for inspection.
//
// Usage: lykoware_rhi_render_target_test OUTPUT_DIR
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan loader, driver or suitable
// device). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/image.hpp>
#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/render_target.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr int exit_skip = 77;
int g_failures = 0;
std::filesystem::path g_output;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

struct Rgb {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

lykoware::rhi::ClearColor clear_of(Rgb rgb)
{
    // k/255 converts to exactly k in 8-bit UNORM on every conforming device.
    return {static_cast<float>(rgb.r) / 255.F, static_cast<float>(rgb.g) / 255.F,
            static_cast<float>(rgb.b) / 255.F, 1.F};
}

lykoware::Image solid(std::uint32_t width, std::uint32_t height, Rgb rgb)
{
    lykoware::Image image;
    if (!image.reset(width, height)) {
        return image;
    }
    auto pixels = image.pixels();
    for (std::size_t index = 0; index + 2 < pixels.size(); index += 3) {
        pixels[index + 0] = rgb.r;
        pixels[index + 1] = rgb.g;
        pixels[index + 2] = rgb.b;
    }
    return image;
}

// Compares and, on mismatch, writes <name>-actual.ppm and <name>-diff.ppm.
void expect_image(const lykoware::Image& expected, const lykoware::Image& actual, std::string_view name)
{
    // Tolerance 0: k/255 clears are exact. A device that rounds differently
    // fails here, which is the point of the check.
    const lykoware::ImageCompareResult result = lykoware::compare_images(expected, actual, 0);
    if (result.matches) {
        return;
    }
    std::cerr << "FAIL: " << name << " differs: size_mismatch=" << result.size_mismatch
              << " mismatched_pixels=" << result.mismatched_pixels << " first=(" << result.first_x << ','
              << result.first_y << ")\n";
    ++g_failures;
    std::error_code ignored;
    std::filesystem::create_directories(g_output, ignored);
    const std::string stem{name};
    if (!lykoware::write_ppm(actual, g_output / (stem + "-actual.ppm"))) {
        std::cerr << "  could not write the actual image\n";
    }
    lykoware::Image diff;
    if (lykoware::make_diff_image(expected, actual, 0, diff)
        && lykoware::write_ppm(diff, g_output / (stem + "-diff.ppm"))) {
        std::cerr << "  wrote " << (g_output / (stem + "-actual.ppm")).string() << " and the diff\n";
    }
}

void test_clear_and_read(const lykoware::rhi::Device& device, std::uint32_t width, std::uint32_t height,
                         Rgb first, Rgb second, std::string_view name)
{
    lykoware::rhi::RhiError error;
    auto target = lykoware::rhi::RenderTarget::create(device, {width, height}, error);
    expect(target != nullptr, "render target is created");
    if (!target) {
        std::cerr << "  " << error.message << '\n';
        return;
    }
    expect(target->width() == width && target->height() == height, "render target reports its extent");

    lykoware::Image unread;
    expect(!target->read_back(unread, error) && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
           "reading back before any render is refused");
    expect(unread.width() == 0 && unread.pixels().empty(), "a refused read-back leaves the image empty");

    expect(!target->last_gpu_time_ns(), "a target that has not rendered has no GPU time");
    const auto before = std::chrono::steady_clock::now();
    expect(target->clear(clear_of(first), error), "first clear succeeds");
    const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - before);
    // ADR 0039: the clear's GPU time lies inside the call, which waits for the GPU.
    const std::optional<std::uint64_t> gpu_ns = target->last_gpu_time_ns();
    expect(gpu_ns.has_value() == device.gpu_timestamps(), "a clear has a GPU time when the device has timestamps");
    const auto wall = static_cast<std::uint64_t>(wall_ns.count());
    expect(!gpu_ns || *gpu_ns <= wall + wall / 10, "the clear's GPU time is no longer than the call that waited for it");
    lykoware::Image actual;
    expect(target->read_back(actual, error), "first read-back succeeds");
    expect(target->last_gpu_time_ns() == gpu_ns, "a read-back keeps the last render's GPU time");
    expect_image(solid(width, height, first), actual, std::string{name} + "-first");

    // A second pass on the same target checks layout tracking after a read-back.
    expect(target->clear(clear_of(second), error), "second clear succeeds");
    const lykoware::rhi::RenderTargetCapture capture{*target};
    lykoware::Image captured;
    expect(capture.capture(captured), "RenderTargetCapture reads through the image harness interface");
    expect_image(solid(width, height, second), captured, std::string{name} + "-second");
}

void test_bad_extents(const lykoware::rhi::Device& device)
{
    for (const lykoware::rhi::RenderTargetDesc desc :
         {lykoware::rhi::RenderTargetDesc{0, 16}, lykoware::rhi::RenderTargetDesc{16, 0},
          lykoware::rhi::RenderTargetDesc{lykoware::rhi::render_target_max_extent + 1, 16}}) {
        lykoware::rhi::RhiError error;
        auto target = lykoware::rhi::RenderTarget::create(device, desc, error);
        expect(target == nullptr && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
               "an extent outside 1..render_target_max_extent is refused");
    }
}

}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "usage: lykoware_rhi_render_target_test OUTPUT_DIR\n";
        return EXIT_FAILURE;
    }
    g_output = argv[1];

    lykoware::rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware rhi.render_target test";
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
    std::cout << "gpu timestamps: " << (device->gpu_timestamps() ? "yes" : "no") << '\n';

    // Square, and odd non-square to catch row-pitch or axis mistakes.
    test_clear_and_read(*device, 16, 16, {64, 128, 192}, {255, 0, 0}, "square-16x16");
    test_clear_and_read(*device, 17, 9, {0, 255, 51}, {12, 34, 56}, "odd-17x9");
    test_bad_extents(*device);

    device.reset();
    if (instance->validation_enabled()) {
        expect(instance->validation_error_count() == 0, "rendering and read-back produce no validation errors");
    } else {
        std::cout << "note: validation layer not installed; validation not checked\n";
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " RHI render-target assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "rhi render target passed\n";
    return EXIT_SUCCESS;
}
