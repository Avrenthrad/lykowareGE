#include <lykoware/input.hpp>

#include "platform_map.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cstdlib>
#include <iostream>
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

std::size_t key_index(lykoware::Key key)
{
    return static_cast<std::size_t>(key);
}

void test_edges()
{
    lykoware::InputState state;
    lykoware::note_key(state, lykoware::Key::A, true);
    const lykoware::ButtonState& key = state.keys[key_index(lykoware::Key::A)];
    expect(key.down && key.pressed && !key.released, "first press sets down and pressed");

    lykoware::begin_input_frame(state);
    expect(key.down && !key.pressed && !key.released, "next frame keeps the level and clears the edge");

    lykoware::note_key(state, lykoware::Key::A, true);
    expect(key.down && !key.pressed, "repeat down does not press again");

    lykoware::note_key(state, lykoware::Key::A, false);
    expect(!key.down && key.released, "release clears down and sets released");

    lykoware::begin_input_frame(state);
    lykoware::note_key(state, lykoware::Key::Enter, true);
    lykoware::note_key(state, lykoware::Key::Enter, false);
    const lykoware::ButtonState& enter = state.keys[key_index(lykoware::Key::Enter)];
    expect(!enter.down && enter.pressed && enter.released, "press and release in one frame keep both edges");

    lykoware::note_key(state, lykoware::Key::Unknown, true);
    lykoware::note_key(state, lykoware::Key::Count, true);
    expect(!state.keys[0].down, "unknown and count are not stored as keys");
}

void test_mouse_and_pad()
{
    lykoware::InputState state;
    lykoware::note_mouse_position(state, 12.f, 34.f);
    lykoware::note_mouse_button(state, lykoware::MouseButton::Right, true);
    lykoware::note_mouse_wheel(state, -1.f, 2.f);
    lykoware::note_mouse_wheel(state, 0.5f, 0.5f);
    expect(state.mouse.x == 12.f && state.mouse.y == 34.f, "mouse position is stored");
    expect(state.mouse.buttons[static_cast<std::size_t>(lykoware::MouseButton::Right)].pressed,
           "right button press is an edge");
    expect(state.mouse.wheel_x == -0.5f && state.mouse.wheel_y == 2.5f, "wheel deltas accumulate");

    lykoware::begin_input_frame(state);
    expect(state.mouse.wheel_x == 0.f && state.mouse.wheel_y == 0.f, "wheel clears each frame");
    expect(state.mouse.x == 12.f, "position survives the frame boundary");

    lykoware::note_gamepad_connection(state, true);
    lykoware::note_gamepad_button(state, lykoware::GamepadButton::South, true);
    lykoware::note_gamepad_axis(state, lykoware::GamepadAxis::LeftX, 0.25f);
    expect(state.gamepad.connected, "gamepad connects");
    expect(state.gamepad.buttons[static_cast<std::size_t>(lykoware::GamepadButton::South)].down,
           "south is down");
    expect(state.gamepad.axes[static_cast<std::size_t>(lykoware::GamepadAxis::LeftX)] == 0.25f,
           "stick axis is stored");

    lykoware::note_gamepad_connection(state, false);
    expect(!state.gamepad.connected, "disconnect clears the pad");
    expect(!state.gamepad.buttons[static_cast<std::size_t>(lykoware::GamepadButton::South)].down,
           "disconnect clears buttons");
    expect(state.gamepad.axes[static_cast<std::size_t>(lykoware::GamepadAxis::LeftX)] == 0.f,
           "disconnect clears axes");
}

void test_axis_scale()
{
    expect(lykoware::gamepad_axis_from_raw(lykoware::GamepadAxis::LeftX, 32767) == 1.f,
           "full stick is 1");
    expect(lykoware::gamepad_axis_from_raw(lykoware::GamepadAxis::LeftX, -32768) == -1.f,
           "negative stick uses the 32768 denominator");
    expect(lykoware::gamepad_axis_from_raw(lykoware::GamepadAxis::LeftX, 40000) == 1.f,
           "stick raw values clamp");
    expect(lykoware::gamepad_axis_from_raw(lykoware::GamepadAxis::RightTrigger, 32767) == 1.f,
           "full trigger is 1");
    expect(lykoware::gamepad_axis_from_raw(lykoware::GamepadAxis::LeftTrigger, -10) == 0.f,
           "a negative trigger is 0");
    expect(lykoware::gamepad_axis_from_raw(lykoware::GamepadAxis::RightTrigger, 0) == 0.f,
           "a resting trigger is 0");
}

void test_platform_map()
{
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_A) == lykoware::Key::A, "A");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_Z) == lykoware::Key::Z, "Z");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_M) == lykoware::Key::M, "M");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_1) == lykoware::Key::Digit1, "1");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_0) == lykoware::Key::Digit0, "0");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_ESCAPE) == lykoware::Key::Escape, "escape");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_RETURN) == lykoware::Key::Enter, "enter");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_SPACE) == lykoware::Key::Space, "space");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_UP) == lykoware::Key::Up, "up");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_LSHIFT) == lykoware::Key::LeftShift,
           "left shift");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_RALT) == lykoware::Key::RightAlt, "right alt");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_F1) == lykoware::Key::F1, "f1");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_F12) == lykoware::Key::F12, "f12");
    expect(lykoware::platform::key_from_scancode(SDL_SCANCODE_CAPSLOCK) == lykoware::Key::Unknown,
           "unmapped scancode");
    expect(lykoware::platform::key_from_scancode(-1) == lykoware::Key::Unknown, "invalid scancode");

    lykoware::MouseButton mouse = lykoware::MouseButton::Count;
    expect(lykoware::platform::mouse_button_from_index(SDL_BUTTON_LEFT, mouse)
               && mouse == lykoware::MouseButton::Left,
           "left mouse");
    expect(lykoware::platform::mouse_button_from_index(SDL_BUTTON_RIGHT, mouse)
               && mouse == lykoware::MouseButton::Right,
           "right mouse");
    expect(lykoware::platform::mouse_button_from_index(SDL_BUTTON_X2, mouse)
               && mouse == lykoware::MouseButton::X2,
           "x2 mouse");
    expect(!lykoware::platform::mouse_button_from_index(0, mouse), "unknown mouse button");

    const int buttons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE,
        SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
        SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
        SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_TOUCHPAD, SDL_GAMEPAD_BUTTON_MISC2,
        SDL_GAMEPAD_BUTTON_MISC3, SDL_GAMEPAD_BUTTON_MISC4, SDL_GAMEPAD_BUTTON_MISC5,
        SDL_GAMEPAD_BUTTON_MISC6
    };
    const lykoware::GamepadButton mapped[] = {
        lykoware::GamepadButton::South, lykoware::GamepadButton::East, lykoware::GamepadButton::West,
        lykoware::GamepadButton::North, lykoware::GamepadButton::Back, lykoware::GamepadButton::Guide,
        lykoware::GamepadButton::Start, lykoware::GamepadButton::LeftStick, lykoware::GamepadButton::RightStick,
        lykoware::GamepadButton::LeftShoulder, lykoware::GamepadButton::RightShoulder,
        lykoware::GamepadButton::DpadUp, lykoware::GamepadButton::DpadDown, lykoware::GamepadButton::DpadLeft,
        lykoware::GamepadButton::DpadRight, lykoware::GamepadButton::Misc1, lykoware::GamepadButton::RightPaddle1,
        lykoware::GamepadButton::LeftPaddle1, lykoware::GamepadButton::RightPaddle2,
        lykoware::GamepadButton::LeftPaddle2, lykoware::GamepadButton::Touchpad, lykoware::GamepadButton::Misc2,
        lykoware::GamepadButton::Misc3, lykoware::GamepadButton::Misc4, lykoware::GamepadButton::Misc5,
        lykoware::GamepadButton::Misc6
    };
    static_assert(sizeof(buttons) / sizeof(buttons[0]) == sizeof(mapped) / sizeof(mapped[0]));
    for (std::size_t index = 0; index < sizeof(buttons) / sizeof(buttons[0]); ++index) {
        lykoware::GamepadButton button = lykoware::GamepadButton::Count;
        const bool ok = lykoware::platform::gamepad_button_from_index(buttons[index], button);
        expect(ok && button == mapped[index], "gamepad button map");
    }
    lykoware::GamepadButton ignored = lykoware::GamepadButton::South;
    expect(!lykoware::platform::gamepad_button_from_index(SDL_GAMEPAD_BUTTON_INVALID, ignored),
           "invalid gamepad button");

    lykoware::GamepadAxis axis = lykoware::GamepadAxis::Count;
    expect(lykoware::platform::gamepad_axis_from_index(SDL_GAMEPAD_AXIS_LEFTX, axis)
               && axis == lykoware::GamepadAxis::LeftX,
           "left stick x");
    expect(lykoware::platform::gamepad_axis_from_index(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axis)
               && axis == lykoware::GamepadAxis::RightTrigger,
           "right trigger");
    expect(!lykoware::platform::gamepad_axis_from_index(SDL_GAMEPAD_AXIS_INVALID, axis),
           "invalid axis");
}

}

int main()
{
    test_edges();
    test_mouse_and_pad();
    test_axis_scale();
    test_platform_map();
    if (g_failures != 0) {
        std::cerr << g_failures << " input assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "input state mapping passed\n";
    return EXIT_SUCCESS;
}
