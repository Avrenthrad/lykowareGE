// rhi.swapchain: the interactive path on a headless window. SDL's offscreen
// video driver gives a Vulkan surface through VK_EXT_headless_surface (Mesa
// lavapipe supports it), so CI exercises acquire, render, present and resize
// with the validation layer watching synchronisation. It also draws the test
// triangle through the swapchain (render_draw), the path the sandbox window
// uses; the triangle's pixels are checked offscreen by rhi.pipeline.
//
// Usage: lykoware_rhi_swapchain_test <triangle.vert.spv> <triangle.frag.spv>
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan, no headless surface, or no
// device that can present). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/window.hpp>

#include <SDL3/SDL.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int exit_skip = 77;
int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

int skip(std::string_view reason)
{
    std::cout << "SKIP: " << reason << '\n';
    return exit_skip;
}

bool render(lykoware::rhi::Swapchain& swapchain, lykoware::Window& window, int frames, float red)
{
    for (int index = 0; index < frames; ++index) {
        window.pump();
        lykoware::rhi::RhiError error;
        if (!swapchain.render_clear({red, 0.25F, 0.5F, 1.F}, error)) {
            std::cerr << "render_clear: " << error.message << '\n';
            return false;
        }
    }
    return true;
}

// ADR 0039: after the device is idle, the next `count` frames from `first` each have a GPU time
// when the device has timestamps, submitted in order between `since` and now, and none are
// returned twice.
void expect_frame_times(const lykoware::rhi::Device& device, lykoware::rhi::Swapchain& swapchain, std::uint64_t first,
                        std::uint64_t count, std::chrono::steady_clock::time_point since, std::string_view what)
{
    lykoware::rhi::RhiError error;
    expect(device.wait_idle(error), "the device goes idle");
    const auto now = std::chrono::steady_clock::now();
    const std::vector<lykoware::rhi::GpuFrameTime> times = swapchain.take_gpu_frame_times();
    if (!device.gpu_timestamps()) {
        expect(times.empty(), "no frame times without timestamps");
        return;
    }
    expect(times.size() == count, std::string{what} + ": one GPU time per frame");
    bool numbered = times.size() == count;
    bool ordered = true;
    bool plausible = !times.empty();
    for (std::size_t index = 0; index < times.size(); ++index) {
        numbered = numbered && times[index].frame == first + index;
        ordered = ordered && times[index].submitted >= since && times[index].submitted <= now
            && (index == 0 || times[index - 1].submitted <= times[index].submitted);
        plausible = plausible && times[index].nanoseconds > 0 && times[index].nanoseconds < 10'000'000'000ULL;
    }
    expect(numbered, std::string{what} + ": frames are numbered in submission order");
    expect(ordered, std::string{what} + ": submission times are in order, while the frames were rendered");
    expect(plausible, std::string{what} + ": GPU times are plausible");
    expect(swapchain.take_gpu_frame_times().empty(), std::string{what} + ": taken times are not returned again");
}

// The window's platform event for `type`, as the platform would send it.
void push_window_event(SDL_EventType type)
{
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    expect(windows != nullptr && count == 1, "one platform window is open");
    if (windows != nullptr && count == 1) {
        SDL_Event event{};
        event.type = type;
        event.window.windowID = SDL_GetWindowID(windows[0]);
        expect(SDL_PushEvent(&event), "push the window event");
    }
    SDL_free(windows);
}

// ADR 0040: frames while minimised are skipped, neither submitted nor presented, and
// restoring resumes with the same swapchain.
void expect_minimised_frames_skipped(const lykoware::rhi::Device& device, lykoware::rhi::Swapchain& swapchain,
                                     lykoware::Window& window)
{
    lykoware::rhi::RhiError error;
    expect(device.wait_idle(error), "the device goes idle");
    (void)swapchain.take_gpu_frame_times();
    const std::uint64_t presented = swapchain.frames_presented();
    const std::uint32_t rebuilds = swapchain.rebuild_count();
    push_window_event(SDL_EVENT_WINDOW_MINIMIZED);
    expect(render(swapchain, window, 3, 0.3F) && window.minimized(), "frames while minimised succeed");
    expect(swapchain.frames_presented() == presented, "a minimised window presents nothing");
    expect(device.wait_idle(error) && swapchain.take_gpu_frame_times().empty(), "and submits nothing");
    push_window_event(SDL_EVENT_WINDOW_RESTORED);
    expect(render(swapchain, window, 2, 0.3F) && !window.minimized(), "frames render after a restore");
    expect(swapchain.frames_presented() == presented + 2, "a restored window presents again");
    expect(swapchain.rebuild_count() == rebuilds, "restoring keeps the swapchain");
}

std::unique_ptr<lykoware::rhi::GraphicsPipeline> triangle_pipeline(const lykoware::rhi::Device& device,
                                                                   const char* vertex_path, const char* fragment_path,
                                                                   lykoware::rhi::ColorFormat format)
{
    using lykoware::rhi::Shader;
    using lykoware::rhi::ShaderStage;
    lykoware::rhi::RhiError error;
    const auto vertex = Shader::load_file(device, vertex_path, ShaderStage::Vertex, error);
    const auto fragment = vertex ? Shader::load_file(device, fragment_path, ShaderStage::Fragment, error) : nullptr;
    if (!vertex || !fragment) {
        std::cerr << "shader: " << error.message << '\n';
        return nullptr;
    }
    lykoware::rhi::GraphicsPipelineDesc desc;
    desc.vertex = vertex.get();
    desc.fragment = fragment.get();
    desc.color_format = format;
    auto pipeline = lykoware::rhi::GraphicsPipeline::create(device, desc, error);
    if (!pipeline) {
        std::cerr << "pipeline: " << error.message << '\n';
    }
    return pipeline;
}

}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        std::cerr << "usage: lykoware_rhi_swapchain_test <triangle.vert.spv> <triangle.frag.spv>\n";
        return EXIT_FAILURE;
    }
    const char* vertex_path = argv[1];
    const char* fragment_path = argv[2];

    lykoware::WindowConfig window_config;
    window_config.title = "lykoware rhi.swapchain test";
    window_config.width = 64;
    window_config.height = 48;
    window_config.visible = false;
    window_config.vulkan = true;
    window_config.driver = lykoware::VideoDriver::Offscreen;
    std::string window_error;
    auto window = lykoware::Window::open(window_config, window_error);
    if (!window) {
        return skip("offscreen Vulkan window unavailable: " + window_error);
    }

    lykoware::rhi::InstanceConfig instance_config;
    instance_config.application_name = "lykoware rhi.swapchain test";
    instance_config.validation = lykoware::rhi::Validation::IfAvailable;
    for (const char* name : window->vulkan_instance_extensions()) {
        instance_config.extensions.emplace_back(name);
    }
    lykoware::rhi::RhiError error;
    auto instance = lykoware::rhi::Instance::create(instance_config, error);
    if (!instance) {
        const bool unavailable = error.failure == lykoware::rhi::RhiFailure::NoLoader
            || error.failure == lykoware::rhi::RhiFailure::NoDriver
            || error.failure == lykoware::rhi::RhiFailure::VersionTooOld
            || error.failure == lykoware::rhi::RhiFailure::MissingExtension;
        if (unavailable) {
            return skip(error.message);
        }
        std::cerr << "FAIL: instance: " << error.message << '\n';
        return EXIT_FAILURE;
    }

    {
        // A device without the swapchain extension is refused up front.
        auto plain = lykoware::rhi::Device::create(*instance, {}, error);
        if (!plain) {
            return skip(error.message);
        }
        auto refused = lykoware::rhi::Swapchain::create(*instance, *plain, *window, {}, error);
        expect(refused == nullptr && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
               "a device without VK_KHR_swapchain is refused");
    }

    lykoware::rhi::DeviceConfig device_config;
    device_config.extensions = {lykoware::rhi::swapchain_device_extension};
    auto device = lykoware::rhi::Device::create(*instance, device_config, error);
    if (!device) {
        return skip(error.message);
    }
    std::cout << "device: " << device->info().name << '\n';

    {
        // Declared before the swapchain so they are destroyed after it: the
        // swapchain destructor waits for frames that still use them.
        std::unique_ptr<lykoware::rhi::GraphicsPipeline> pipeline;
        std::unique_ptr<lykoware::rhi::GraphicsPipeline> wrong_format;
        auto swapchain = lykoware::rhi::Swapchain::create(*instance, *device, *window, {}, error);
        if (!swapchain && error.failure == lykoware::rhi::RhiFailure::NoSuitableDevice) {
            return skip(error.message);
        }
        expect(swapchain != nullptr, "a vsync swapchain is created on the offscreen window");
        if (!swapchain) {
            std::cerr << "  " << error.message << '\n';
        } else {
            expect(swapchain->width() == 64 && swapchain->height() == 48, "swapchain extent matches the window");
            expect(swapchain->image_count() >= 2, "swapchain has at least two images");
            const auto first_frames = std::chrono::steady_clock::now();
            expect(render(*swapchain, *window, 5, 0.1F), "five frames render and present");
            expect(swapchain->frames_presented() == 5, "five frames are presented");
            expect_frame_times(*device, *swapchain, 0, 5, first_frames, "the first five frames");

            const auto later_frames = std::chrono::steady_clock::now();
            std::string resize_error;
            expect(window->resize(80, 60, resize_error), "the window resizes");
            const std::uint32_t rebuilds_before = swapchain->rebuild_count();
            expect(render(*swapchain, *window, 3, 0.6F), "frames render after a resize");
            expect(swapchain->width() == 80 && swapchain->height() == 60, "swapchain follows the new window size");
            expect(swapchain->rebuild_count() > rebuilds_before, "a resize rebuilds the swapchain");
            expect(swapchain->frames_presented() == 8, "every frame after the resize is presented");

            using lykoware::rhi::ColorFormat;
            const ColorFormat format = swapchain->color_format();
            pipeline = triangle_pipeline(*device, vertex_path, fragment_path, format);
            expect(pipeline != nullptr, "a triangle pipeline is built for the swapchain's colour format");
            if (pipeline) {
                bool drawn = true;
                for (int index = 0; index < 4 && drawn; ++index) {
                    window->pump();
                    drawn = swapchain->render_draw({0.2F, 0.2F, 0.2F, 1.F}, *pipeline, 3, error);
                    if (!drawn) {
                        std::cerr << "render_draw: " << error.message << '\n';
                    }
                }
                expect(drawn, "the triangle draws through the swapchain");
                expect(swapchain->frames_presented() == 12, "every drawn frame is presented");
                expect_frame_times(*device, *swapchain, 5, 7, later_frames, "frames across a resize and the triangle");
            }

            const ColorFormat other = format == ColorFormat::Bgra8Unorm ? ColorFormat::Rgba8Unorm
                                                                        : ColorFormat::Bgra8Unorm;
            wrong_format = triangle_pipeline(*device, vertex_path, fragment_path, other);
            expect(wrong_format != nullptr, "a pipeline for the other colour format builds");
            if (wrong_format) {
                expect(!swapchain->render_draw({0.F, 0.F, 0.F, 1.F}, *wrong_format, 3, error)
                           && error.failure == lykoware::rhi::RhiFailure::InvalidArgument,
                       "a pipeline for another colour format is refused");
                expect(swapchain->frames_presented() == 12, "a refused draw presents nothing");
                expect(swapchain->take_gpu_frame_times().empty(), "a refused draw times nothing");
            }
        }
    }

    {
        lykoware::rhi::SwapchainConfig no_vsync;
        no_vsync.vsync = false;
        auto swapchain = lykoware::rhi::Swapchain::create(*instance, *device, *window, no_vsync, error);
        expect(swapchain != nullptr, "a non-vsync swapchain is created after the first is destroyed");
        if (swapchain) {
            expect(render(*swapchain, *window, 4, 0.9F), "non-vsync frames render and present");
            expect_minimised_frames_skipped(*device, *swapchain, *window);
        }
    }

    device.reset();
    if (instance->validation_enabled()) {
        expect(instance->validation_error_count() == 0, "presenting produces no validation errors");
    } else {
        std::cout << "note: validation layer not installed; synchronisation not validated\n";
    }
    instance.reset();
    window.reset();

    if (g_failures != 0) {
        std::cerr << g_failures << " RHI swapchain assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "rhi swapchain passed\n";
    return EXIT_SUCCESS;
}
