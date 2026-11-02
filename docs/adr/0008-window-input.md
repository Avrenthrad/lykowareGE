# ADR 0008 — Window and input

Status: Proposed
Related task/PR: branch feat/module02-window-input, stacked on feat/phase0-image-harness (PR #7).
Supersedes / superseded by: none. This does not implement the graphics API. ADR 0002 accepts Vulkan 1.3 behind an owned RHI; this wrapper does not link Vulkan. This does not add a timing or fixed-step clock.

## Context

Phase 1 needs a window, an input snapshot, and a way for a later Vulkan RHI to create a surface. The process has to stay headless in CI. The licence policy allows a vendored zlib/MIT/BSD/Apache library behind a Lykoware API. SDL is the library the brief names for this job.

## Options

- Keep the console sandbox and add a window only when the RHI arrives.
- Call Win32 and X11 directly and grow a second backend later.
- Vendor SDL3, pin a release, and hide it behind an engine window and input API, with the dummy video driver for tests.
- Vendor SDL2. It is the older API and does not match the SDL3 window and gamepad calls this wrapper uses.

## Decision

Proposed, not accepted: vendor SDL 3.4.18 under `third_party/sdl3` and wrap it with `lykoware::Window` and `lykoware::InputState`. The release tag is `release-3.4.18`, commit `829a65d769d935c4852f8159e964312c0957260a`. Provenance, the zlib licence, and the HIDAPI licence election are in `third_party/sdl3/PROVENANCE.md`.

Public engine headers do not include SDL and do not mention SDL types. The static library is linked privately. Vulkan is not a dependency. A window can report the instance extensions SDL would require and can create or destroy a surface when the caller passes a `VkInstance` as `void*`. Opening that path fails with a reason when no loader is present.

Tests use `VideoDriver::Dummy`, so they do not need a display. `lykoware_sandbox --window` can open a platform window. `--window --dummy --frames N` is the headless form and is what the foundation smoke test runs. `lyko` is unchanged and still launches the sandbox with no arguments.

The owner has not accepted this choice. Replacing SDL means reimplementing the same engine API.

## Contracts and compatibility

Ownership, lifetime, threading, and failure are specified on the declarations in [engine/include/lykoware/window.hpp](../../engine/include/lykoware/window.hpp) and [engine/include/lykoware/input.hpp](../../engine/include/lykoware/input.hpp).

One process shares one platform session and one video driver. The first successful `Window::open` starts it. The last window destructor stops it. `pump()` does not block. It starts an input frame, then applies the events that are waiting. Keyboard edges, mouse buttons, the wheel, and the first gamepad are part of that snapshot. Further gamepads are ignored until that pad disconnects; then the next connected pad takes over. The platform queue is process-wide, so a pump files each event into a per-window mailbox and each window applies only its own events, plus quit, gamepad, and window-less input, which every window receives. A new window adopts an already-connected pad.

Client size is a request. `resize` asks the platform and then reads the size back. Display scale is one value for both axes. `dpi_x` and `dpi_y` are that scale times 96. The dummy driver reports a positive scale; it is not a monitor measurement.

The Vulkan hook does not include Vulkan headers. On the supported 64-bit targets the opaque instance and surface handles are pointer-sized. The caller destroys a surface before destroying the window. Native window handles are also exposed for a backend that creates the surface itself. Neither path links Vulkan, and neither accepts ADR 0002.

The vendored tree omits upstream directories the CMake build does not compile. Those omissions are listed in the provenance file. No present upstream source file is patched.

## Consequences

CI can create, resize, pump, and close a window without a display, and it can map keyboard, mouse, and gamepad input. A desktop Linux window is available only when X11 or Wayland headers were present at configure time. Without them the build still succeeds and the dummy driver remains. Windows builds the Win32 driver. Optional X11 extras that would fail configure on a partial install are off.

HIDAPI, which SDL bundles, is used under its BSD-style licence. The GPL alternative in that directory is not elected.

This does not provide a renderer, a swapchain, focus-loss policy beyond the events SDL already delivers, or a frame clock. `lyko screenshot` and `lyko run --scene` stay exit 3.

## Verification

`input.mapping` covers edge rules, axis scaling, and the SDL 3.4.18 scancode, mouse, and gamepad integer map. `window.lifecycle` opens a dummy window, checks size, DPI, resize, close, reopen, keyboard and mouse through the pump, a virtual gamepad, and the Vulkan hook's refusal without an instance. `foundation.smoke` runs `lykoware_sandbox --window --dummy --frames 1`. Record platform results in STATUS. A green run does not accept this ADR and does not tick Phase 1.
