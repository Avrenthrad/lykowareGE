#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lykoware {

// Keyboard, mouse, and one gamepad snapshot. The platform backend fills this
// through the functions below. Platform types are not part of this header.
//
// Coordinates: mouse position is window-relative pixels. The origin is the
// top-left of the client area. X increases to the right. Y increases downward.
// Wheel X is positive to the right. Wheel Y is positive away from the user.
// Values may lie outside the window.
//
// Gamepad axes are normalised. Sticks are -1..1. Triggers are 0..1.
// The raw platform axis is a 16-bit signed value, -32768..32767.
//
// Ownership: InputState owns only its own fields. note_* and begin_input_frame
// do not retain pointers. A caller that passes InputState& keeps it.
//
// Lifetime: the functions are synchronous. Edges (pressed, released) and the
// wheel deltas last until the next begin_input_frame. Level state (down,
// position, axes, connection) persists across frames.
//
// Threading: one InputState is used from one thread. There is no lock.
// No function here starts a thread.
//
// Failure: these functions do not fail and do not throw. An unknown key,
// button, or axis is ignored. A null-sized or out-of-range raw axis is
// clamped into the 16-bit range before it is scaled.

enum class Key : std::uint16_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,
    Escape, Enter, Space, Tab, Backspace,
    Left, Right, Up, Down,
    LeftShift, RightShift,
    LeftCtrl, RightCtrl,
    LeftAlt, RightAlt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

enum class MouseButton : std::uint8_t {
    Left,
    Right,
    Middle,
    X1,
    X2,
    Count
};

// Face buttons are physical positions, not the legend printed on a pad.
enum class GamepadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Misc1,
    RightPaddle1,
    LeftPaddle1,
    RightPaddle2,
    LeftPaddle2,
    Touchpad,
    Misc2,
    Misc3,
    Misc4,
    Misc5,
    Misc6,
    Count
};

enum class GamepadAxis : std::uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

// down is the level. pressed and released are edges since begin_input_frame.
// A press and release in the same frame leaves both edges true and down false.
struct ButtonState {
    bool down = false;
    bool pressed = false;
    bool released = false;
};

struct MouseState {
    std::array<ButtonState, static_cast<std::size_t>(MouseButton::Count)> buttons{};
    float x = 0.f;
    float y = 0.f;
    float wheel_x = 0.f;
    float wheel_y = 0.f;
};

// One pad. Additional pads are ignored until this one disconnects; then the
// next connected pad, if any, takes over with released buttons and centred
// axes until it reports a change.
struct GamepadState {
    bool connected = false;
    std::array<ButtonState, static_cast<std::size_t>(GamepadButton::Count)> buttons{};
    std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
};

struct InputState {
    std::array<ButtonState, static_cast<std::size_t>(Key::Count)> keys{};
    MouseState mouse{};
    GamepadState gamepad{};
};

// Drops pressed, released, and wheel deltas. Levels are kept.
void begin_input_frame(InputState& state) noexcept;

void note_key(InputState& state, Key key, bool down) noexcept;
void note_mouse_button(InputState& state, MouseButton button, bool down) noexcept;
void note_mouse_position(InputState& state, float x, float y) noexcept;
void note_mouse_wheel(InputState& state, float delta_x, float delta_y) noexcept;
void note_gamepad_connection(InputState& state, bool connected) noexcept;
void note_gamepad_button(InputState& state, GamepadButton button, bool down) noexcept;
void note_gamepad_axis(InputState& state, GamepadAxis axis, float value) noexcept;

// Sticks return -1..1. Triggers return 0..1 and treat a negative raw value as 0.
// raw is clamped to -32768..32767 first.
[[nodiscard]] float gamepad_axis_from_raw(GamepadAxis axis, int raw) noexcept;

}
