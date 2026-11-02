#include "platform_map.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

namespace lykoware::platform {
namespace {

static_assert(static_cast<int>(Key::Z) - static_cast<int>(Key::A) == 25);
static_assert(static_cast<int>(Key::F12) - static_cast<int>(Key::F1) == 11);

}

Key key_from_scancode(int scancode) noexcept
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (scancode - SDL_SCANCODE_A));
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (scancode - SDL_SCANCODE_F1));
    }
    switch (scancode) {
    case SDL_SCANCODE_1: return Key::Digit1;
    case SDL_SCANCODE_2: return Key::Digit2;
    case SDL_SCANCODE_3: return Key::Digit3;
    case SDL_SCANCODE_4: return Key::Digit4;
    case SDL_SCANCODE_5: return Key::Digit5;
    case SDL_SCANCODE_6: return Key::Digit6;
    case SDL_SCANCODE_7: return Key::Digit7;
    case SDL_SCANCODE_8: return Key::Digit8;
    case SDL_SCANCODE_9: return Key::Digit9;
    case SDL_SCANCODE_0: return Key::Digit0;
    case SDL_SCANCODE_RETURN: return Key::Enter;
    case SDL_SCANCODE_ESCAPE: return Key::Escape;
    case SDL_SCANCODE_BACKSPACE: return Key::Backspace;
    case SDL_SCANCODE_TAB: return Key::Tab;
    case SDL_SCANCODE_SPACE: return Key::Space;
    case SDL_SCANCODE_LEFT: return Key::Left;
    case SDL_SCANCODE_RIGHT: return Key::Right;
    case SDL_SCANCODE_UP: return Key::Up;
    case SDL_SCANCODE_DOWN: return Key::Down;
    case SDL_SCANCODE_LSHIFT: return Key::LeftShift;
    case SDL_SCANCODE_RSHIFT: return Key::RightShift;
    case SDL_SCANCODE_LCTRL: return Key::LeftCtrl;
    case SDL_SCANCODE_RCTRL: return Key::RightCtrl;
    case SDL_SCANCODE_LALT: return Key::LeftAlt;
    case SDL_SCANCODE_RALT: return Key::RightAlt;
    default: return Key::Unknown;
    }
}

bool mouse_button_from_index(int index, MouseButton& button) noexcept
{
    switch (index) {
    case SDL_BUTTON_LEFT: button = MouseButton::Left; return true;
    case SDL_BUTTON_RIGHT: button = MouseButton::Right; return true;
    case SDL_BUTTON_MIDDLE: button = MouseButton::Middle; return true;
    case SDL_BUTTON_X1: button = MouseButton::X1; return true;
    case SDL_BUTTON_X2: button = MouseButton::X2; return true;
    default: return false;
    }
}

bool gamepad_button_from_index(int index, GamepadButton& button) noexcept
{
    switch (index) {
    case SDL_GAMEPAD_BUTTON_SOUTH: button = GamepadButton::South; return true;
    case SDL_GAMEPAD_BUTTON_EAST: button = GamepadButton::East; return true;
    case SDL_GAMEPAD_BUTTON_WEST: button = GamepadButton::West; return true;
    case SDL_GAMEPAD_BUTTON_NORTH: button = GamepadButton::North; return true;
    case SDL_GAMEPAD_BUTTON_BACK: button = GamepadButton::Back; return true;
    case SDL_GAMEPAD_BUTTON_GUIDE: button = GamepadButton::Guide; return true;
    case SDL_GAMEPAD_BUTTON_START: button = GamepadButton::Start; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: button = GamepadButton::LeftStick; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: button = GamepadButton::RightStick; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: button = GamepadButton::LeftShoulder; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: button = GamepadButton::RightShoulder; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: button = GamepadButton::DpadUp; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: button = GamepadButton::DpadDown; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: button = GamepadButton::DpadLeft; return true;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: button = GamepadButton::DpadRight; return true;
    case SDL_GAMEPAD_BUTTON_MISC1: button = GamepadButton::Misc1; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: button = GamepadButton::RightPaddle1; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: button = GamepadButton::LeftPaddle1; return true;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: button = GamepadButton::RightPaddle2; return true;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: button = GamepadButton::LeftPaddle2; return true;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: button = GamepadButton::Touchpad; return true;
    case SDL_GAMEPAD_BUTTON_MISC2: button = GamepadButton::Misc2; return true;
    case SDL_GAMEPAD_BUTTON_MISC3: button = GamepadButton::Misc3; return true;
    case SDL_GAMEPAD_BUTTON_MISC4: button = GamepadButton::Misc4; return true;
    case SDL_GAMEPAD_BUTTON_MISC5: button = GamepadButton::Misc5; return true;
    case SDL_GAMEPAD_BUTTON_MISC6: button = GamepadButton::Misc6; return true;
    default: return false;
    }
}

bool gamepad_axis_from_index(int index, GamepadAxis& axis) noexcept
{
    switch (index) {
    case SDL_GAMEPAD_AXIS_LEFTX: axis = GamepadAxis::LeftX; return true;
    case SDL_GAMEPAD_AXIS_LEFTY: axis = GamepadAxis::LeftY; return true;
    case SDL_GAMEPAD_AXIS_RIGHTX: axis = GamepadAxis::RightX; return true;
    case SDL_GAMEPAD_AXIS_RIGHTY: axis = GamepadAxis::RightY; return true;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: axis = GamepadAxis::LeftTrigger; return true;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: axis = GamepadAxis::RightTrigger; return true;
    default: return false;
    }
}

}
