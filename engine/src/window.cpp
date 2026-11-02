#include <lykoware/window.hpp>

#include "platform_map.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstddef>
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

// Events waiting for one window. The platform event queue is process-wide,
// so whichever window pumps first drains it and files each event here.
// A window applies only its own mailbox, after it starts its input frame,
// so edges for another window survive until that window pumps.
struct Mailbox {
    SDL_WindowID window_id = 0;
    std::vector<SDL_Event> pending;
};

// Bounds memory for a window that is open but never pumped. Events beyond
// this are dropped for that window only.
constexpr std::size_t mailbox_limit = 4096;

struct Session {
    int holders = 0;
    VideoDriver driver = VideoDriver::Platform;
    bool started = false;
    std::vector<Mailbox*> mailboxes;
};

Session& session()
{
    static Session value;
    return value;
}

// SDL driver name for the headless drivers; nullptr lets SDL choose.
const char* forced_driver_name(VideoDriver driver) noexcept
{
    switch (driver) {
    case VideoDriver::Dummy: return "dummy";
    case VideoDriver::Offscreen: return "offscreen";
    case VideoDriver::Platform: return nullptr;
    }
    return nullptr;
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
    const char* const forced = forced_driver_name(driver);
    if (forced != nullptr) {
        const char* existing = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
        const bool already = existing != nullptr && std::string_view{existing} == forced;
        if (!already && !SDL_SetHint(SDL_HINT_VIDEO_DRIVER, forced)) {
            error = std::string{"could not select the "} + forced + " video driver";
            return false;
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        error = sdl_failure("SDL_Init");
        return false;
    }
    if (forced != nullptr) {
        const char* name = SDL_GetCurrentVideoDriver();
        if (name == nullptr || std::string_view{name} != forced) {
            SDL_Quit();
            error = std::string{forced} + " video driver was not selected";
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

void mailbox_register(Mailbox& mailbox)
{
    session().mailboxes.push_back(&mailbox);
}

void mailbox_unregister(Mailbox& mailbox) noexcept
{
    auto& boxes = session().mailboxes;
    boxes.erase(std::remove(boxes.begin(), boxes.end(), &mailbox), boxes.end());
}

// The window an event belongs to. Zero means every open window: quit,
// gamepad, and input events the platform did not tie to a window.
// Returns false for events no window uses.
bool event_target(const SDL_Event& event, SDL_WindowID& target) noexcept
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        target = 0;
        return true;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        target = event.window.windowID;
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        target = event.key.windowID;
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        target = event.motion.windowID;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        target = event.button.windowID;
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        target = event.wheel.windowID;
        return true;
    default:
        return false;
    }
}

void deliver(Mailbox& mailbox, const SDL_Event& event)
{
    if (mailbox.pending.size() < mailbox_limit) {
        mailbox.pending.push_back(event);
    }
}

// Drains the process-wide queue into every open window's mailbox.
void dispatch_events()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        SDL_WindowID target = 0;
        if (!event_target(event, target)) {
            continue;
        }
        for (Mailbox* mailbox : session().mailboxes) {
            if (target == 0 || mailbox->window_id == target) {
                deliver(*mailbox, event);
            }
        }
    }
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
        if (registered) {
            mailbox_unregister(mailbox);
            registered = false;
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

    bool open_gamepad(SDL_JoystickID id)
    {
        gamepad = SDL_OpenGamepad(id);
        if (gamepad == nullptr) {
            return false;
        }
        gamepad_id = id;
        gamepad_open = true;
        note_gamepad_connection(input, true);
        return true;
    }

    // Adopts the first connected pad other than `excluded`. Used when the
    // window opens (pads attached earlier sent their added event before this
    // window existed) and when the active pad disconnects while another
    // remains. Button and axis levels start released and centred; they
    // update on the pad's next change.
    void adopt_connected_gamepad(SDL_JoystickID excluded)
    {
        if (gamepad_open) {
            return;
        }
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids == nullptr) {
            return;
        }
        for (int index = 0; index < count; ++index) {
            if (ids[index] != excluded && open_gamepad(ids[index])) {
                break;
            }
        }
        SDL_free(ids);
    }

    void apply(const SDL_Event& event)
    {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            close = true;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!event.key.repeat) {
                note_key(input, platform::key_from_scancode(event.key.scancode), event.key.down);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            note_mouse_position(input, event.motion.x, event.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            note_mouse_position(input, event.button.x, event.button.y);
            MouseButton button = MouseButton::Left;
            if (platform::mouse_button_from_index(event.button.button, button)) {
                note_mouse_button(input, button, event.button.down);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float delta_x = event.wheel.x;
            float delta_y = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                delta_x = -delta_x;
                delta_y = -delta_y;
            }
            note_mouse_position(input, event.wheel.mouse_x, event.wheel.mouse_y);
            note_mouse_wheel(input, delta_x, delta_y);
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_open) {
                open_gamepad(event.gdevice.which);
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad_open && event.gdevice.which == gamepad_id) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                gamepad_open = false;
                note_gamepad_connection(input, false);
                adopt_connected_gamepad(event.gdevice.which);
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

    void poll()
    {
        dispatch_events();
        // Move out first: applying an event never re-enters dispatch, but a
        // local copy keeps the loop independent of the mailbox's storage.
        std::vector<SDL_Event> events;
        events.swap(mailbox.pending);
        for (const SDL_Event& event : events) {
            apply(event);
        }
    }

    SDL_Window* window = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID gamepad_id = 0;
    SDL_WindowID window_id = 0;
    bool gamepad_open = false;
    bool session_held = false;
    bool registered = false;
    Mailbox mailbox;
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
    impl->mailbox.window_id = impl->window_id;
    mailbox_register(impl->mailbox);
    impl->registered = true;
    impl->adopt_connected_gamepad(0);
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
    } else if (driver != nullptr && std::string_view{driver} == "offscreen") {
        native.kind = NativeWindowKind::Offscreen;
    }
    return native;
}

}
