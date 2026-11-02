// rhi.swapchain: the interactive path on a headless window. SDL's offscreen
// video driver gives a Vulkan surface through VK_EXT_headless_surface (Mesa
// lavapipe supports it), so CI exercises acquire, render, present and resize
// with the validation layer watching synchronisation.
//
// Exit codes: 0 pass, 1 fail, 77 skip (no Vulkan, no headless surface, or no
// device that can present). CTest reports 77 as Skipped, never as Passed.

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/swapchain.hpp>
#include <lykoware/window.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

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

}

int main()
{
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
            expect(render(*swapchain, *window, 5, 0.1F), "five frames render and present");
            expect(swapchain->frames_presented() == 5, "five frames are presented");

            std::string resize_error;
            expect(window->resize(80, 60, resize_error), "the window resizes");
            const std::uint32_t rebuilds_before = swapchain->rebuild_count();
            expect(render(*swapchain, *window, 3, 0.6F), "frames render after a resize");
            expect(swapchain->width() == 80 && swapchain->height() == 60, "swapchain follows the new window size");
            expect(swapchain->rebuild_count() > rebuilds_before, "a resize rebuilds the swapchain");
            expect(swapchain->frames_presented() == 8, "every frame after the resize is presented");
        }
    }

    {
        lykoware::rhi::SwapchainConfig no_vsync;
        no_vsync.vsync = false;
        auto swapchain = lykoware::rhi::Swapchain::create(*instance, *device, *window, no_vsync, error);
        expect(swapchain != nullptr, "a non-vsync swapchain is created after the first is destroyed");
        if (swapchain) {
            expect(render(*swapchain, *window, 4, 0.9F), "non-vsync frames render and present");
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
