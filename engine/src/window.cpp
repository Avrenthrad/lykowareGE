#include <lykoware/window.hpp>

#include "platform_map.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <string_view>
#include <utility>
#include <vector>

namespace lykoware {
namespace {

std::string sdl_failure(const char* action)
{
    const char* message = SDL_GetError();
    if (message == nullptr || message[0] == '\0') {
        return std::string{action} + " failed";
    }
    return std::string{action} + " failed: " + message;
}

struct Session {
    int holders = 0;
    VideoDriver driver = VideoDriver::Platform;
    bool started = false;
};

Session& session()
{
    static Session value;
    return value;
}

bool session_acquire(VideoDriver driver, std::string& error)
{
    Session& current = session();
    if (current.started) {
        if (current.driver != driver) {
            error = "a window is already open with a different video driver";
            return false;
        }
        ++current.holders;
        return true;
    }
    if (driver == VideoDriver::Dummy) {
        const char* existing = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
        const bool already_dummy = existing != nullptr && std::string_view{existing} == "dummy";
        if (!already_dummy && !SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy")) {
            error = "could not select the dummy video driver";
            return false;
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        error = sdl_failure("SDL_Init");
        return false;
    }
    if (driver == VideoDriver::Dummy) {
        const char* name = SDL_GetCurrentVideoDriver();
        if (name == nullptr || std::string_view{name} != "dummy") {
            SDL_Quit();
            error = "dummy video driver was not selected";
            return false;
        }
    }
    current.started = true;
    current.driver = driver;
    current.holders = 1;
    return true;
}

void session_release()
{
    Session& current = session();
    if (current.holders <= 0) {
        return;
    }
    --current.holders;
    if (current.holders == 0) {
        SDL_Quit();
        current.started = false;
    }
}

bool size_allowed(int width, int height) noexcept
{
    return width >= 1 && height >= 1 && width <= window_max_extent && height <= window_max_extent;
}

}

struct Window::Impl {
    ~Impl()
    {
        if (gamepad != nullptr) {
            SDL_CloseGamepad(gamepad);
            gamepad = nullptr;
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        if (session_held) {
            session_release();
            session_held = false;
        }
    }

    void refresh_metrics()
    {
        int next_width = 0;
        int next_height = 0;
        if (SDL_GetWindowSize(window, &next_width, &next_height)) {
            if (next_width > 0) {
                width = next_width;
            }
            if (next_height > 0) {
                height = next_height;
            }
        }
        const float scale = SDL_GetWindowDisplayScale(window);
        if (scale > 0.f) {
            display_scale = scale;
        }
        const float density = SDL_GetWindowPixelDensity(window);
        if (density > 0.f) {
            pixel_density = density;
        }
    }

    void capture_extensions()
    {
        extensions.clear();
        unsigned int count = 0;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (names == nullptr || count == 0) {
            return;
        }
        extensions.assign(names, names + count);
    }

    void poll()
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                close = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == window_id) {
                    close = true;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!event.key.repeat && (event.key.windowID == 0 || event.key.windowID == window_id)) {
                    note_key(input, platform::key_from_scancode(event.key.scancode), event.key.down);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (event.motion.windowID == 0 || event.motion.windowID == window_id) {
                    note_mouse_position(input, event.motion.x, event.motion.y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.windowID == 0 || event.button.windowID == window_id) {
                    note_mouse_position(input, event.button.x, event.button.y);
                    MouseButton button = MouseButton::Left;
                    if (platform::mouse_button_from_index(event.button.button, button)) {
                        note_mouse_button(input, button, event.button.down);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (event.wheel.windowID == 0 || event.wheel.windowID == window_id) {
                    float delta_x = event.wheel.x;
                    float delta_y = event.wheel.y;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                        delta_x = -delta_x;
                        delta_y = -delta_y;
                    }
                    note_mouse_position(input, event.wheel.mouse_x, event.wheel.mouse_y);
                    note_mouse_wheel(input, delta_x, delta_y);
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!gamepad_open) {
                    gamepad = SDL_OpenGamepad(event.gdevice.which);
                    if (gamepad != nullptr) {
                        gamepad_id = event.gdevice.which;
                        gamepad_open = true;
                        note_gamepad_connection(input, true);
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (gamepad_open && event.gdevice.which == gamepad_id) {
                    SDL_CloseGamepad(gamepad);
                    gamepad = nullptr;
                    gamepad_open = false;
                    note_gamepad_connection(input, false);
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (gamepad_open && event.gbutton.which == gamepad_id) {
                    GamepadButton button = GamepadButton::South;
                    if (platform::gamepad_button_from_index(event.gbutton.button, button)) {
                        note_gamepad_button(input, button, event.gbutton.down);
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (gamepad_open && event.gaxis.which == gamepad_id) {
                    GamepadAxis axis = GamepadAxis::LeftX;
                    if (platform::gamepad_axis_from_index(event.gaxis.axis, axis)) {
                        note_gamepad_axis(input, axis, gamepad_axis_from_raw(axis, event.gaxis.value));
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    SDL_Window* window = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID gamepad_id = 0;
    SDL_WindowID window_id = 0;
    bool gamepad_open = false;
    bool session_held = false;
    bool close = false;
    bool vulkan = false;
    int width = 0;
    int height = 0;
    float display_scale = 1.f;
    float pixel_density = 1.f;
    InputState input{};
    std::vector<const char*> extensions;
};

Window::Window(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)}
{
}

Window::~Window() = default;

std::unique_ptr<Window> Window::open(const WindowConfig& config, std::string& error)
{
    error.clear();
    if (!size_allowed(config.width, config.height)) {
        error = "window size must be from 1 to " + std::to_string(window_max_extent);
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    if (!session_acquire(config.driver, error)) {
        return nullptr;
    }
    impl->session_held = true;
    impl->vulkan = config.vulkan;
    impl->width = config.width;
    impl->height = config.height;

    const std::string title_text = config.title.empty() ? std::string{"Lykoware"} : config.title;

    SDL_WindowFlags flags = 0;
    if (!config.visible) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }

    impl->window = SDL_CreateWindow(title_text.c_str(), config.width, config.height, flags);
    if (impl->window == nullptr) {
        error = sdl_failure("SDL_CreateWindow");
        return nullptr;
    }
    impl->window_id = SDL_GetWindowID(impl->window);
    impl->refresh_metrics();
    if (config.vulkan) {
        impl->capture_extensions();
    }
    return std::unique_ptr<Window>(new Window(std::move(impl)));
}

int Window::width() const
{
    return impl_->width;
}

int Window::height() const
{
    return impl_->height;
}

float Window::display_scale() const
{
    return impl_->display_scale;
}

float Window::pixel_density() const
{
    return impl_->pixel_density;
}

float Window::dpi_x() const
{
    return impl_->display_scale * window_reference_dpi;
}

float Window::dpi_y() const
{
    return impl_->display_scale * window_reference_dpi;
}

bool Window::close_requested() const
{
    return impl_->close;
}

const InputState& Window::input() const
{
    return impl_->input;
}

bool Window::pump()
{
    begin_input_frame(impl_->input);
    impl_->poll();
    impl_->refresh_metrics();
    return !impl_->close;
}

void Window::request_close()
{
    impl_->close = true;
}

bool Window::resize(int width, int height, std::string& error)
{
    error.clear();
    if (!size_allowed(width, height)) {
        error = "window size must be from 1 to " + std::to_string(window_max_extent);
        return false;
    }
    if (!SDL_SetWindowSize(impl_->window, width, height)) {
        error = sdl_failure("SDL_SetWindowSize");
        return false;
    }
    if (!SDL_SyncWindow(impl_->window)) {
        impl_->refresh_metrics();
        error = sdl_failure("SDL_SyncWindow");
        return false;
    }
    impl_->refresh_metrics();
    return true;
}

std::span<const char* const> Window::vulkan_instance_extensions() const
{
    return impl_->extensions;
}

bool Window::create_vulkan_surface(VulkanInstanceHandle instance,
                                   const void* allocator,
                                   VulkanSurfaceHandle& surface,
                                   std::string& error)
{
    error.clear();
    surface = nullptr;
    if (!impl_->vulkan) {
        error = "window was not created for a Vulkan surface";
        return false;
    }
    if (instance == nullptr) {
        error = "Vulkan instance is required";
        return false;
    }
    VkSurfaceKHR created = nullptr;
    const auto* callbacks = static_cast<const VkAllocationCallbacks*>(allocator);
    if (!SDL_Vulkan_CreateSurface(impl_->window,
                                  static_cast<VkInstance>(instance),
                                  callbacks,
                                  &created)
        || created == nullptr) {
        error = sdl_failure("SDL_Vulkan_CreateSurface");
        surface = nullptr;
        return false;
    }
    surface = static_cast<void*>(created);
    return true;
}

void Window::destroy_vulkan_surface(VulkanInstanceHandle instance,
                                    VulkanSurfaceHandle surface,
                                    const void* allocator)
{
    if (!impl_->vulkan || instance == nullptr || surface == nullptr) {
        return;
    }
    SDL_Vulkan_DestroySurface(static_cast<VkInstance>(instance),
                              static_cast<VkSurfaceKHR>(surface),
                              static_cast<const VkAllocationCallbacks*>(allocator));
}

NativeWindow Window::native_window() const
{
    NativeWindow native;
    if (impl_->window == nullptr) {
        return native;
    }
    const SDL_PropertiesID properties = SDL_GetWindowProperties(impl_->window);
#if defined(_WIN32)
    native.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (native.window != nullptr) {
        native.kind = NativeWindowKind::Win32;
        return native;
    }
#elif defined(__APPLE__)
    native.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (native.window != nullptr) {
        native.kind = NativeWindowKind::Cocoa;
        return native;
    }
#else
    native.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    const Sint64 xid = SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    if (native.display != nullptr && xid != 0) {
        native.kind = NativeWindowKind::X11;
        native.window_id = static_cast<std::uint64_t>(xid);
        return native;
    }
    native.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    native.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    if (native.display != nullptr || native.window != nullptr) {
        native.kind = NativeWindowKind::Wayland;
        return native;
    }
    native.display = nullptr;
    native.window = nullptr;
#endif
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver != nullptr && std::string_view{driver} == "dummy") {
        native.kind = NativeWindowKind::Dummy;
    }
    return native;
}

}
