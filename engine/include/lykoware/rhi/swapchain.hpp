#pragma once

#include <lykoware/rhi/device.hpp>
#include <lykoware/rhi/instance.hpp>
#include <lykoware/rhi/buffer.hpp>
#include <lykoware/rhi/pipeline.hpp>
#include <lykoware/rhi/render_target.hpp>
#include <lykoware/window.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace lykoware::rhi {

// Vulkan device extension a Device needs before a Swapchain can be created.
inline constexpr const char* swapchain_device_extension = "VK_KHR_swapchain";

// Frames the CPU may record ahead of the GPU.
inline constexpr std::uint32_t frames_in_flight = 2;

// How long one frame's commands took on the GPU (ADR 0039). `frame` counts
// the frames submitted before it, from 0; frames skipped while minimised or
// out of date are not submitted and not counted. `submitted` is when the CPU
// submitted it; the GPU starts on it no earlier.
struct GpuFrameTime {
    std::uint64_t frame = 0;
    std::uint64_t nanoseconds = 0;
    std::chrono::steady_clock::time_point submitted{};
};

struct SwapchainConfig {
    // true: FIFO (always available, no tearing). false: mailbox if offered,
    // then immediate, then FIFO.
    bool vsync = true;
    // Also keep a 32-bit float depth buffer the size of the swapchain, for
    // pipelines with depth (ADR 0025). Rebuilt with the swapchain.
    bool depth = false;
};

// The interactive path: presents dynamic-rendering passes to a Window's
// Vulkan surface, using the same attachment and barrier pattern as
// RenderTarget (ADR 0011). Images are 8-bit UNORM, like RenderTarget: BGRA
// or RGBA, chosen once at create() and kept across rebuilds, so a pipeline
// built for color_format() stays usable for the swapchain's lifetime.
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
// extension is InvalidArgument; a queue that cannot present, or a surface
// that offers no 8-bit UNORM BGRA or RGBA format, is NoSuitableDevice;
// surface creation and driver refusals are DriverError. Nothing throws for
// those; std::bad_alloc can propagate.
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

    // Format pipelines must target to draw into this swapchain.
    [[nodiscard]] ColorFormat color_format() const;

    // Acquires an image, clears it through a dynamic-rendering pass and
    // presents it. Returns false only on a failure it cannot recover from
    // by rebuilding the swapchain.
    [[nodiscard]] bool render_clear(const ClearColor& color, RhiError& error);

    // As render_clear(), then draws `vertex_count` vertices with `pipeline`
    // (no vertex buffers) in the same pass, with a full-extent viewport and
    // scissor. A pipeline built for another colour format is InvalidArgument
    // and nothing is acquired or submitted. Up to frames_in_flight frames
    // may still use the pipeline after this returns: destroy the Swapchain
    // (its destructor waits for the device), or call Device::wait_idle(),
    // before destroying the pipeline.
    [[nodiscard]] bool render_draw(const ClearColor& clear, const GraphicsPipeline& pipeline,
                                   std::uint32_t vertex_count, RhiError& error);

    [[nodiscard]] bool has_depth() const;

    // As render_clear(), then draws `meshes` with `pipeline` in the same pass,
    // depth-tested when the pipeline uses depth. The same checks as
    // RenderTarget::draw_meshes refuse misuse with InvalidArgument before
    // anything is acquired. The pipeline and every buffer and texture must
    // stay alive until the GPU is done with them (see render_draw).
    [[nodiscard]] bool render_meshes(const ClearColor& clear, const GraphicsPipeline& pipeline,
                                     std::span<const MeshDraw> meshes, RhiError& error);

    // Frames actually presented, and how many times the swapchain was rebuilt.
    [[nodiscard]] std::uint64_t frames_presented() const;
    [[nodiscard]] std::uint32_t rebuild_count() const;

    // GPU times of the frames that finished since the last call, oldest
    // first, measured with timestamps at the start and end of each frame's
    // commands (ADR 0039). A frame's time can include waiting for its image
    // to be released by the display. Frames still on the GPU are returned by
    // a later call; at most 1024 are kept between calls, the oldest dropped
    // first. Always empty when the device has no timestamps
    // (Device::gpu_timestamps()).
    [[nodiscard]] std::vector<GpuFrameTime> take_gpu_frame_times();

private:
    struct Impl;
    explicit Swapchain(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
