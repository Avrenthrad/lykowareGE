#include <lykoware/input.hpp>

namespace lykoware {
namespace {

void clear_edges(ButtonState& button) noexcept
{
    button.pressed = false;
    button.released = false;
}

void note_button(ButtonState& button, bool down) noexcept
{
    if (down) {
        if (!button.down) {
            button.pressed = true;
        }
        button.down = true;
        return;
    }
    if (button.down) {
        button.down = false;
        button.released = true;
    }
}

template <typename Enum, std::size_t N>
bool slot(Enum value, std::size_t& index) noexcept
{
    const auto raw = static_cast<std::size_t>(value);
    if (raw >= N) {
        return false;
    }
    index = raw;
    return true;
}

}

void release_keys_and_mouse_buttons(InputState& state) noexcept
{
    for (ButtonState& key : state.keys) {
        note_button(key, false);
    }
    for (ButtonState& button : state.mouse.buttons) {
        note_button(button, false);
    }
}

void begin_input_frame(InputState& state) noexcept
{
    for (ButtonState& key : state.keys) {
        clear_edges(key);
    }
    for (ButtonState& button : state.mouse.buttons) {
        clear_edges(button);
    }
    state.mouse.wheel_x = 0.f;
    state.mouse.wheel_y = 0.f;
    for (ButtonState& button : state.gamepad.buttons) {
        clear_edges(button);
    }
}

void note_key(InputState& state, Key key, bool down) noexcept
{
    if (key == Key::Unknown) {
        return;
    }
    std::size_t index = 0;
    if (!slot<Key, static_cast<std::size_t>(Key::Count)>(key, index)) {
        return;
    }
    note_button(state.keys[index], down);
}

void note_mouse_button(InputState& state, MouseButton button, bool down) noexcept
{
    std::size_t index = 0;
    if (!slot<MouseButton, static_cast<std::size_t>(MouseButton::Count)>(button, index)) {
        return;
    }
    note_button(state.mouse.buttons[index], down);
}

void note_mouse_position(InputState& state, float x, float y) noexcept
{
    state.mouse.x = x;
    state.mouse.y = y;
}

void note_mouse_wheel(InputState& state, float delta_x, float delta_y) noexcept
{
    state.mouse.wheel_x += delta_x;
    state.mouse.wheel_y += delta_y;
}

void note_gamepad_connection(InputState& state, bool connected) noexcept
{
    if (!connected) {
        state.gamepad = {};
        return;
    }
    state.gamepad.connected = true;
}

void note_gamepad_button(InputState& state, GamepadButton button, bool down) noexcept
{
    std::size_t index = 0;
    if (!slot<GamepadButton, static_cast<std::size_t>(GamepadButton::Count)>(button, index)) {
        return;
    }
    note_button(state.gamepad.buttons[index], down);
}

void note_gamepad_axis(InputState& state, GamepadAxis axis, float value) noexcept
{
    const auto index = static_cast<std::size_t>(axis);
    if (index >= state.gamepad.axes.size()) {
        return;
    }
    state.gamepad.axes[index] = value;
}

float gamepad_axis_from_raw(GamepadAxis axis, int raw) noexcept
{
    if (raw > 32767) {
        raw = 32767;
    }
    if (raw < -32768) {
        raw = -32768;
    }
    const bool trigger = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
    if (trigger) {
        if (raw < 0) {
            raw = 0;
        }
        return static_cast<float>(raw) / 32767.f;
    }
    if (raw < 0) {
        return static_cast<float>(raw) / 32768.f;
    }
    return static_cast<float>(raw) / 32767.f;
}

}
