#include <lykoware/window.hpp>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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
    test_vulkan_without_loader();
    if (g_failures != 0) {
        std::cerr << g_failures << " window assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "window lifecycle passed\n";
    return EXIT_SUCCESS;
}
