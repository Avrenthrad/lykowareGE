#pragma once

#include <lykoware/input.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace lykoware {

// Largest client size accepted by open() and resize(). This is an argument
// guard, not a display mode and not a renderer limit.
inline constexpr int window_max_extent = 16384;

// Reference DPI used to turn the platform display scale into dots per inch.
// display_scale() == 1 means 96 DPI. Both axes use that one scale. The
// platform reports a single display scale, so dpi_x and dpi_y match.
inline constexpr float window_reference_dpi = 96.f;

enum class VideoDriver {
    // The platform default (Win32, X11, Wayland, Cocoa, or an environment
    // override the platform library already understands).
    Platform,
    // No display. Tests and CI use this. It does not open a desktop window.
    // It cannot host a Vulkan surface.
    Dummy,
    // No display, but the window can host a Vulkan surface through
    // VK_EXT_headless_surface when the driver offers it (Mesa lavapipe does).
    // Used to test the swapchain path in CI.
    Offscreen
};

enum class NativeWindowKind {
    None,
    Win32,
    X11,
    Wayland,
    Cocoa,
    Dummy,
    Offscreen
};

// Handles a future RHI can pass to a platform surface call without this
// engine linking Vulkan. On the supported 64-bit targets, VkInstance and
// VkSurfaceKHR are pointer-sized. allocator is an optional
// VkAllocationCallbacks const* and may be null.
struct NativeWindow {
    NativeWindowKind kind = NativeWindowKind::None;
    void* display = nullptr;
    void* window = nullptr;
    // X11 window id when it is not stored as a pointer. Zero otherwise.
    std::uint64_t window_id = 0;
};

using VulkanInstanceHandle = void*;
using VulkanSurfaceHandle = void*;

struct WindowConfig {
    std::string title = "Lykoware";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool visible = true;
    // Ask the platform for a Vulkan surface. open() fails if that request
    // cannot be satisfied, including when no Vulkan loader is installed.
    // The engine still does not link Vulkan.
    bool vulkan = false;
    VideoDriver driver = VideoDriver::Platform;
};

// One OS window, or a dummy window when driver is Dummy.
//
// Ownership: open() returns the sole owner. The destructor closes the
// platform window. Copy and move are not provided. input() refers to state
// owned by the window.
//
// Lifetime: pump() is synchronous and does not block. Edge input lasts until
// the next pump(), which starts a new input frame. vulkan_instance_extensions()
// pointers remain valid until this window is destroyed. A Vulkan surface
// created here must be destroyed with destroy_vulkan_surface() before the
// window destructor runs. The caller owns that surface.
//
// Threading: create, pump, resize, and destroy a window on one thread.
// There is no internal lock and no background thread. The first window
// initialises the process-wide platform session. Every window in the
// process shares that session and its video driver. Platform events arrive
// on one process-wide queue: any window's pump() files them per window, and
// each window applies only its own events (plus quit, gamepad, and input not
// tied to a window, which every window receives). A window that is never
// pumped holds at most a bounded backlog; later events for it are dropped.
// A new window adopts the first gamepad that is already connected.
//
// Failure: open() and resize() and create_vulkan_surface() return failure
// and write a reason to error. On success they clear error. They do not
// throw for a platform refusal. std::bad_alloc can propagate from allocation.
// pump() returns false when close was requested. It still updates input
// and size before returning.
class Window {
public:
    [[nodiscard]] static std::unique_ptr<Window> open(const WindowConfig& config, std::string& error);

    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] float display_scale() const;
    [[nodiscard]] float pixel_density() const;
    [[nodiscard]] float dpi_x() const;
    [[nodiscard]] float dpi_y() const;
    [[nodiscard]] bool close_requested() const;
    [[nodiscard]] const InputState& input() const;

    // Poll platform events, start a new input frame, then apply those events.
    // Returns false after request_close() or a platform close/quit.
    bool pump();
    void request_close();

    [[nodiscard]] bool resize(int width, int height, std::string& error);

    // Instance extension names required before a surface can be created.
    // Empty when the window was not opened with vulkan, or when the platform
    // reported none. Pointers are owned by the platform session.
    [[nodiscard]] std::span<const char* const> vulkan_instance_extensions() const;

    // instance is VkInstance. allocator is const VkAllocationCallbacks* or
    // null. On success, surface receives VkSurfaceKHR as a pointer.
    [[nodiscard]] bool create_vulkan_surface(VulkanInstanceHandle instance,
                                             const void* allocator,
                                             VulkanSurfaceHandle& surface,
                                             std::string& error);

    void destroy_vulkan_surface(VulkanInstanceHandle instance,
                                VulkanSurfaceHandle surface,
                                const void* allocator);

    [[nodiscard]] NativeWindow native_window() const;

private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}
