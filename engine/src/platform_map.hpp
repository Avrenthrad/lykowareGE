#pragma once

// Internal map from the vendored SDL3 scancode and gamepad integers onto
// Lykoware input values. This header is not a public engine header and it
// does not include SDL. The integers are the SDL_Scancode, SDL mouse button,
// SDL_GamepadButton, and SDL_GamepadAxis values from SDL 3.4.18.

#include <lykoware/input.hpp>

namespace lykoware::platform {

[[nodiscard]] Key key_from_scancode(int scancode) noexcept;
[[nodiscard]] bool mouse_button_from_index(int index, MouseButton& button) noexcept;
[[nodiscard]] bool gamepad_button_from_index(int index, GamepadButton& button) noexcept;
[[nodiscard]] bool gamepad_axis_from_index(int index, GamepadAxis& axis) noexcept;

}
