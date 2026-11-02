#pragma once

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/window.hpp>

#include <cstdint>
#include <memory>

namespace lykoware::rhi {

// Vulkan device extension a Device needs before a Swapchain can be created.
inline constexpr const char* swapchain_device_extension = "VK_KHR_swapchain";

// Frames the CPU may record ahead of the GPU.
inline constexpr std::uint32_t frames_in_flight = 2;

struct SwapchainConfig {
    // true: FIFO (always available, no tearing). false: mailbox if offered,
    // then immediate, then FIFO.
    bool vsync = true;
};

// The interactive path: presents dynamic-rendering passes to a Window's
// Vulkan surface, using the same attachment and barrier pattern as
// RenderTarget (ADR 0011). Images are 8-bit UNORM, like RenderTarget.
//
// Prerequisites: the Window was opened with WindowConfig::vulkan, the
// Instance enables the Window's vulkan_instance_extensions(), and the Device
// enables swapchain_device_extension and can present from its queue.
//
// Ownership: create() returns the sole owner. The swapchain owns the window
// surface it creates, its swapchain images and views, and per-frame command
// buffers, fences and semaphores. Copy and move are not provided.
//
// Lifetime: the Window, Device and Instance must outlive the Swapchain.
// The destructor waits for the device to go idle, destroys everything it
// owns and then the surface.
//
// Threading: one thread, the Device's thread. render_clear() blocks only
// when frames_in_flight frames are already queued, or on vsync.
//
// Resize: each frame compares the surface/window pixel size with the
// swapchain extent and rebuilds the swapchain when they differ, or when the
// driver reports it out of date or suboptimal. A zero-sized (minimised)
// window skips presentation until it has an area again.
//
// Failure: create() returns nullptr and fills error. A device without the
// extension is InvalidArgument; a queue that cannot present is
// NoSuitableDevice; surface creation and driver refusals are DriverError.
// Nothing throws for those; std::bad_alloc can propagate.
class Swapchain {
public:
    [[nodiscard]] static std::unique_ptr<Swapchain> create(
        const Instance& instance, const Device& device, Window& window, const SwapchainConfig& config,
        RhiError& error);

    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    // Current swapchain extent in pixels. Zero while the window has no area.
    [[nodiscard]] std::uint32_t width() const;
    [[nodiscard]] std::uint32_t height() const;
    [[nodiscard]] std::uint32_t image_count() const;

    // Acquires an image, clears it through a dynamic-rendering pass and
    // presents it. Returns false only on a failure it cannot recover from
    // by rebuilding the swapchain.
    [[nodiscard]] bool render_clear(const ClearColor& color, RhiError& error);

    // Frames actually presented, and how many times the swapchain was rebuilt.
    [[nodiscard]] std::uint64_t frames_presented() const;
    [[nodiscard]] std::uint32_t rebuild_count() const;

private:
    struct Impl;
    explicit Swapchain(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
