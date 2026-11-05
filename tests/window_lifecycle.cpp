#include <lykoware/window.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

lykoware::WindowConfig dummy_config()
{
    lykoware::WindowConfig config;
    config.title = "lykoware headless";
    config.width = 320;
    config.height = 240;
    config.visible = false;
    config.resizable = true;
    config.vulkan = false;
    config.driver = lykoware::VideoDriver::Dummy;
    return config;
}

void push_key(SDL_Scancode scancode, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.repeat = false;
    expect(SDL_PushEvent(&event), "push key event");
}

void test_lifecycle()
{
    std::string error;
    auto window = lykoware::Window::open(dummy_config(), error);
    expect(window != nullptr, "dummy window opens without a display");
    if (!window) {
        std::cerr << "open error: " << error << '\n';
        return;
    }
    expect(window->width() == 320 && window->height() == 240, "client size matches the request");
    expect(window->display_scale() > 0.f && std::isfinite(window->display_scale()), "display scale is finite");
    expect(window->pixel_density() > 0.f && std::isfinite(window->pixel_density()), "pixel density is finite");
    expect(window->dpi_x() == window->display_scale() * lykoware::window_reference_dpi, "dpi x uses 96");
    expect(window->dpi_y() == window->dpi_x(), "both axes share the display scale");
    expect(!window->close_requested(), "a new window is open");
    expect(window->native_window().kind == lykoware::NativeWindowKind::Dummy, "dummy native kind");
    expect(window->vulkan_instance_extensions().empty(), "non-vulkan window has no instance extensions");
    lykoware::VulkanSurfaceHandle surface = nullptr;
    expect(!window->create_vulkan_surface(nullptr, nullptr, surface, error), "surface hook rejects a missing instance");
    expect(surface == nullptr, "failed surface output stays null");
    expect(error.find("Vulkan") != std::string::npos, "surface failure names Vulkan");

    expect(window->resize(400, 300, error), "resize requests a new client size");
    if (!error.empty()) {
        std::cerr << "resize error: " << error << '\n';
    }
    expect(window->pump(), "pump after resize stays open");
    expect(window->width() == 400 && window->height() == 300, "size follows the resize");

    push_key(SDL_SCANCODE_A, true);
    expect(window->pump(), "pump reads the key");
    const auto& pressed = window->input().keys[static_cast<std::size_t>(lykoware::Key::A)];
    expect(pressed.down && pressed.pressed && !pressed.released, "pumped key is down and pressed");
    expect(window->pump(), "second pump");
    expect(pressed.down && !pressed.pressed, "held key does not press again");

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 8.f;
    motion.motion.y = 9.f;
    expect(SDL_PushEvent(&motion), "push motion");
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_LEFT;
    click.button.down = true;
    click.button.x = 8.f;
    click.button.y = 9.f;
    expect(SDL_PushEvent(&click), "push click");
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.f;
    wheel.wheel.mouse_x = 8.f;
    wheel.wheel.mouse_y = 9.f;
    wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    expect(SDL_PushEvent(&wheel), "push wheel");
    expect(window->pump(), "pump mouse");
    expect(window->input().mouse.x == 8.f && window->input().mouse.y == 9.f, "mouse position from the platform");
    expect(window->input().mouse.buttons[static_cast<std::size_t>(lykoware::MouseButton::Left)].pressed,
           "left button maps through the pump");
    expect(window->input().mouse.wheel_y == 1.f, "wheel maps through the pump");

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = 0xffffffffu;
    desc.axis_mask = 0xffffffffu;
    // Vendor and product match SDL's built-in Shield mapping so the virtual
    // device is recognised as a gamepad. They are test data, not a product dependency.
    desc.vendor_id = 0x0955;
    desc.product_id = 0x7210;
    desc.name = "Lykoware test gamepad";
    const SDL_JoystickID device = SDL_AttachVirtualJoystick(&desc);
    expect(device != 0, "virtual gamepad attaches");
    bool connected = false;
    for (int attempt = 0; attempt < 5 && !connected; ++attempt) {
        expect(window->pump(), "pump gamepad attach");
        connected = window->input().gamepad.connected;
    }
    expect(connected, "first gamepad connects through the pump");
    if (connected) {
        SDL_Joystick* joystick = SDL_OpenJoystick(device);
        expect(joystick != nullptr, "virtual joystick opens");
        if (joystick != nullptr) {
            expect(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true),
                   "virtual south button");
            expect(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 32767),
                   "virtual left stick");
            expect(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767),
                   "virtual trigger");
            bool south = false;
            float stick = 0.f;
            float trigger = 0.f;
            for (int attempt = 0; attempt < 5 && !south; ++attempt) {
                expect(window->pump(), "pump gamepad state");
                const lykoware::GamepadState& pad = window->input().gamepad;
                south = pad.buttons[static_cast<std::size_t>(lykoware::GamepadButton::South)].down;
                stick = pad.axes[static_cast<std::size_t>(lykoware::GamepadAxis::LeftX)];
                trigger = pad.axes[static_cast<std::size_t>(lykoware::GamepadAxis::RightTrigger)];
            }
            expect(south, "south button reaches input state");
            expect(stick == 1.f, "stick axis is normalised");
            expect(trigger == 1.f, "trigger axis is normalised");
            SDL_CloseJoystick(joystick);
        }
    }
    if (device != 0) {
        SDL_DetachVirtualJoystick(device);
    }

    window->request_close();
    expect(window->close_requested(), "close can be requested");
    expect(!window->pump(), "pump reports the close");

    lykoware::WindowConfig rejected = dummy_config();
    rejected.width = 0;
    auto bad = lykoware::Window::open(rejected, error);
    expect(bad == nullptr && !error.empty(), "a zero size is rejected");

    window.reset();
    auto again = lykoware::Window::open(dummy_config(), error);
    expect(again != nullptr, "a window can be opened again after the last one closes");
    if (!again) {
        std::cerr << "reopen error: " << error << '\n';
    }
}

SDL_JoystickID attach_virtual_gamepad(const char* name)
{
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = 0xffffffffu;
    desc.axis_mask = 0xffffffffu;
    // Same Shield mapping as test_lifecycle, so SDL treats it as a gamepad.
    desc.vendor_id = 0x0955;
    desc.product_id = 0x7210;
    desc.name = name;
    return SDL_AttachVirtualJoystick(&desc);
}

void pump_times(lykoware::Window& window, int count)
{
    for (int index = 0; index < count; ++index) {
        window.pump();
    }
}

// Every SDL window ID currently open, for picking out the newest window.
std::vector<SDL_WindowID> open_window_ids()
{
    std::vector<SDL_WindowID> ids;
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (windows != nullptr) {
        for (int index = 0; index < count; ++index) {
            ids.push_back(SDL_GetWindowID(windows[index]));
        }
        SDL_free(windows);
    }
    return ids;
}

void test_second_gamepad_takes_over()
{
    std::string error;
    auto window = lykoware::Window::open(dummy_config(), error);
    expect(window != nullptr, "window opens for the gamepad handover test");
    if (!window) {
        return;
    }
    const SDL_JoystickID first = attach_virtual_gamepad("Lykoware pad A");
    pump_times(*window, 5);
    const SDL_JoystickID second = attach_virtual_gamepad("Lykoware pad B");
    pump_times(*window, 5);
    expect(first != 0 && second != 0, "both virtual gamepads attach");
    expect(window->input().gamepad.connected, "first pad is connected");

    SDL_DetachVirtualJoystick(first);
    pump_times(*window, 5);
    expect(window->input().gamepad.connected,
           "a remaining pad takes over when the active pad disconnects");

    // A window opened after both pads attached never saw their added events.
    auto later = lykoware::Window::open(dummy_config(), error);
    expect(later != nullptr, "second window opens");
    if (later) {
        expect(later->input().gamepad.connected, "a new window adopts an already-connected pad");
    }

    SDL_DetachVirtualJoystick(second);
    pump_times(*window, 5);
    expect(!window->input().gamepad.connected, "no pad is connected after the last one detaches");
}

void test_events_reach_their_window()
{
    std::string error;
    auto first = lykoware::Window::open(dummy_config(), error);
    const std::vector<SDL_WindowID> before = open_window_ids();
    auto second = lykoware::Window::open(dummy_config(), error);
    expect(first != nullptr && second != nullptr, "two windows open together");
    if (!first || !second) {
        return;
    }
    SDL_WindowID second_id = 0;
    for (const SDL_WindowID id : open_window_ids()) {
        if (std::find(before.begin(), before.end(), id) == before.end()) {
            second_id = id;
        }
    }
    expect(second_id != 0, "second window has its own platform ID");

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = second_id;
    event.key.scancode = SDL_SCANCODE_B;
    event.key.down = true;
    expect(SDL_PushEvent(&event), "push key for the second window");

    // The first window drains the shared queue before the second pumps.
    first->pump();
    second->pump();
    const auto key = static_cast<std::size_t>(lykoware::Key::B);
    expect(!first->input().keys[key].down, "a key for another window is not applied here");
    expect(second->input().keys[key].down && second->input().keys[key].pressed,
           "a key for this window survives another window's pump with its edge");

    push_key(SDL_SCANCODE_C, true);
    first->pump();
    second->pump();
    const auto shared = static_cast<std::size_t>(lykoware::Key::C);
    expect(first->input().keys[shared].down && second->input().keys[shared].down,
           "an event with no window reaches every window");
}

void push_window_event(SDL_EventType type, SDL_WindowID id)
{
    SDL_Event event{};
    event.type = type;
    event.window.windowID = id;
    expect(SDL_PushEvent(&event), "push window event");
}

// Opens a dummy window and finds its platform ID: the one that was not open before.
std::unique_ptr<lykoware::Window> open_with_id(SDL_WindowID& id)
{
    const std::vector<SDL_WindowID> before = open_window_ids();
    std::string error;
    auto window = lykoware::Window::open(dummy_config(), error);
    id = 0;
    for (const SDL_WindowID candidate : open_window_ids()) {
        if (std::find(before.begin(), before.end(), candidate) == before.end()) {
            id = candidate;
        }
    }
    expect(window != nullptr && id != 0, "a window opens with its own platform ID");
    return window;
}

// ADR 0040: focus follows its window's events, and losing it releases held keys and buttons.
void test_focus()
{
    SDL_WindowID first_id = 0;
    SDL_WindowID second_id = 0;
    auto first = open_with_id(first_id);
    auto second = open_with_id(second_id);
    if (!first || !second) {
        return;
    }
    expect(!first->has_focus() && !second->has_focus(), "hidden headless windows start without focus");

    push_window_event(SDL_EVENT_WINDOW_FOCUS_GAINED, first_id);
    first->pump();
    second->pump();
    expect(first->has_focus() && !second->has_focus(), "focus reaches only its own window");

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.windowID = first_id;
    key.key.scancode = SDL_SCANCODE_A;
    key.key.down = true;
    expect(SDL_PushEvent(&key), "push key for the focused window");
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.windowID = first_id;
    click.button.button = SDL_BUTTON_LEFT;
    click.button.down = true;
    expect(SDL_PushEvent(&click), "push click for the focused window");
    first->pump();
    second->pump();
    const auto a = static_cast<std::size_t>(lykoware::Key::A);
    const auto left = static_cast<std::size_t>(lykoware::MouseButton::Left);
    expect(first->input().keys[a].down && first->input().mouse.buttons[left].down, "the key and button are held");

    // Focus moves to the other window, which receives the key-up instead.
    push_window_event(SDL_EVENT_WINDOW_FOCUS_LOST, first_id);
    push_window_event(SDL_EVENT_WINDOW_FOCUS_GAINED, second_id);
    key.type = SDL_EVENT_KEY_UP;
    key.key.windowID = second_id;
    key.key.down = false;
    expect(SDL_PushEvent(&key), "push the key-up to the newly focused window");
    first->pump();
    second->pump();
    expect(!first->has_focus() && second->has_focus(), "focus moves to the second window");
    const lykoware::ButtonState& held_key = first->input().keys[a];
    const lykoware::ButtonState& held_button = first->input().mouse.buttons[left];
    expect(!held_key.down && held_key.released, "losing focus releases a held key, with an edge");
    expect(!held_button.down && held_button.released, "losing focus releases a held mouse button, with an edge");
    const lykoware::ButtonState& other_key = second->input().keys[a];
    expect(!other_key.down && !other_key.released, "a key-up for a key never seen down has no edge");
    first->pump();
    expect(!held_key.released && !held_button.released, "the release edges last one pump");
}

// ADR 0040: minimised follows its window's events; maximise or restore ends it.
void test_minimise()
{
    SDL_WindowID id = 0;
    auto window = open_with_id(id);
    if (!window) {
        return;
    }
    expect(!window->minimized(), "a new window is not minimised");
    push_window_event(SDL_EVENT_WINDOW_MINIMIZED, id);
    window->pump();
    expect(window->minimized(), "a minimise event minimises the window");
    expect(window->width() == 320 && window->height() == 240, "a minimised window keeps its size");
    push_window_event(SDL_EVENT_WINDOW_RESTORED, id);
    window->pump();
    expect(!window->minimized(), "a restore event ends it");
    push_window_event(SDL_EVENT_WINDOW_MINIMIZED, id);
    window->pump();
    push_window_event(SDL_EVENT_WINDOW_MAXIMIZED, id);
    window->pump();
    expect(!window->minimized(), "a maximise event ends it too");
}

// ADR 0040: a platform close request closes only its window; quit closes every window.
void test_close_events()
{
    SDL_WindowID first_id = 0;
    SDL_WindowID second_id = 0;
    auto first = open_with_id(first_id);
    auto second = open_with_id(second_id);
    if (!first || !second) {
        return;
    }
    push_window_event(SDL_EVENT_WINDOW_CLOSE_REQUESTED, second_id);
    expect(first->pump(), "a close request for another window leaves this one open");
    expect(!second->pump() && second->close_requested(), "a platform close request closes its window");
    expect(!first->close_requested(), "the other window is still open");

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    expect(SDL_PushEvent(&quit), "push quit");
    expect(!first->pump() && first->close_requested(), "quit closes every window");
    expect(!first->pump(), "a closed window stays closed");
}

void test_offscreen_driver()
{
    // Headless like Dummy, but able to host a Vulkan surface (rhi.swapchain
    // covers that). Here: it opens, reports its kind, and resizes.
    lykoware::WindowConfig config = dummy_config();
    config.driver = lykoware::VideoDriver::Offscreen;
    std::string error;
    auto window = lykoware::Window::open(config, error);
    expect(window != nullptr, "offscreen window opens without a display");
    if (!window) {
        std::cerr << "offscreen error: " << error << '\n';
        return;
    }
    expect(window->native_window().kind == lykoware::NativeWindowKind::Offscreen, "offscreen native kind");
    expect(window->resize(100, 50, error) && window->width() == 100 && window->height() == 50,
           "offscreen window resizes");
    expect(window->pump(), "offscreen window pumps");
}

void test_vulkan_without_loader()
{
    lykoware::WindowConfig config = dummy_config();
    config.vulkan = true;
    std::string error;
    auto window = lykoware::Window::open(config, error);
    if (!window) {
        expect(!error.empty(), "a missing Vulkan loader fails with a reason");
        return;
    }
    expect(!window->vulkan_instance_extensions().empty(), "a Vulkan window reports instance extensions");
    lykoware::VulkanSurfaceHandle surface = nullptr;
    expect(!window->create_vulkan_surface(nullptr, nullptr, surface, error),
           "a null instance does not create a surface");
}

}

int main()
{
    test_lifecycle();
    test_second_gamepad_takes_over();
    test_events_reach_their_window();
    test_focus();
    test_minimise();
    test_close_events();
    test_offscreen_driver();
    test_vulkan_without_loader();
    if (g_failures != 0) {
        std::cerr << g_failures << " window assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "window lifecycle passed\n";
    return EXIT_SUCCESS;
}
