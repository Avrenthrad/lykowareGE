# ADR 0040 — Window focus, minimise, close and restart

Status: Proposed.
Related task/PR: window lifecycle (branch `claude/window-lifecycle`), stacked on GPU timings ([ADR 0039](0039-gpu-timings.md)).
Supersedes / superseded by: none. Extends the window of [ADR 0008](0008-window-input.md) and the swapchain of [ADR 0012](0012-swapchain.md).

## Context

Phase 1's exit asks that "resize/focus/close and restart tests pass". Before this ADR:

- **Resize and a reopened window** were tested (`window.lifecycle`, `rhi.swapchain`).
- **Focus:** the window did not track it. A key held when focus moved to another window of ours stayed down for good, because its key-up went to that other window. SDL resets the keyboard only when focus leaves the application.
- **Minimise:** the window did not track it either.
  - The swapchain skipped frames only when the surface reported no area, which Windows does for a minimised window and X11 does not.
  - With FIFO presentation, some platforms block presentation while the window is hidden, so a minimised sandbox could stall or busy-loop.
- **Close:** only `request_close()` was tested. A platform close request and quit were not.
- **Restart:** nothing tested a full shutdown and start of the engine's systems in one process.

## Options

- **Where focus loss releases input:**
  - **in the window**, as it applies the focus-lost event;
  - **left to the game.**

  The window was chosen: every game would need the same fix, and only the window sees the event in order with the input events.
- **How minimise reaches rendering:**
  - **the swapchain asks its window**;
  - **every game checks the window** before drawing.

  The swapchain was chosen. It already holds the window and already skips frames when there is no area, so games need no change.
- **How to test restart:**
  - **in one process**, with each system destroyed and created again three times;
  - **by running the sandbox several times.**

  One process was chosen: a second start in a fresh process proves nothing about leftover state.

## Decision

Proposed:

- **`Window`:**
  - **`has_focus()`** follows `SDL_EVENT_WINDOW_FOCUS_GAINED` and `FOCUS_LOST`. It starts from the platform's flag when the window opens.
  - **Losing focus** releases every held key and mouse button, each with a released edge, through the new `release_keys_and_mouse_buttons(InputState&)`. Gamepads are untouched, because pads keep reporting in the background.
  - **`minimized()`** follows `SDL_EVENT_WINDOW_MINIMIZED`, and `RESTORED` or `MAXIMIZED` end it. It also starts from the platform's flag.
  - **These window events** go to their own window only, like key and mouse events.
  - **A platform close request** closes only its window, and quit closes every window. Nothing reopens a closed window.
- **`Swapchain`:**
  - A frame while the window is minimised is skipped: not submitted, not presented, and the render call returns true.
  - The swapchain is kept, so restoring needs no rebuild.
  - A window with no area still releases the swapchain until it has one again.
- **Sandbox:** while minimised it sleeps 16 ms a loop instead of spinning. The simulation keeps running, and `--frames` still counts these loops, so a run cannot hang.
- **New tests:**
  - **`lifecycle.restart`** (no GPU, runs on every platform). Three cycles of: a headless window, audio on SDL's dummy driver playing a tone, a physics world and an ECS world. Each cycle:
    - the window closes through a platform close request;
    - the window and audio shut down in alternating order;
    - nothing of SDL is left running afterwards (`SDL_WasInit(0) == 0`);
    - physics gives a bit-identical result every cycle.
  - **`lifecycle.restart_vulkan`.** The same plus the whole Vulkan stack on an offscreen window: instance, device, swapchain with depth and the lit renderer. Each of the three cycles:
    - draws a crate for five frames;
    - presents all five;
    - times each on the GPU where supported;
    - leaves no validation error.

    It exits 77 (skipped) without Vulkan, and fails if Vulkan disappears after a restart.

## Contracts and compatibility

- **New:**
  - `Window::has_focus()` and `Window::minimized()`;
  - `release_keys_and_mouse_buttons(InputState&)`.
- **Changed:**
  - A window that loses focus now releases held keys and mouse buttons. Before, they stayed down.
  - A swapchain on a minimised window now skips frames. Before, it rendered, or blocked on platforms that hold presentation.
- **Not yet:**
  - Wayland compositors do not tell applications they are minimised, so there the swapchain still depends on presentation not blocking;
  - pausing the simulation or audio on focus loss or minimise is left to games;
  - a real desktop window's focus and minimise are untested in CI, where the tests push the platform's events instead.

## Consequences

- With `window.lifecycle`, `rhi.swapchain` and the two restart tests, the exit item's four parts (resize, focus, close, restart) have tests. Whether the item counts as done is the owner's call.
- No stuck keys or buttons after switching windows.
- No busy loop while minimised.

## Verification

- **`window.lifecycle`**, three new cases:
  - **Focus** (two windows):
    - focus events reach only their window;
    - a held key and mouse button are released with edges when focus moves to the other window, which gets the key-up;
    - a key-up for a key never seen down makes no edge;
    - the edges last one pump.
  - **Minimise:** minimise, restore and maximise events; the size is kept while minimised.
  - **Close:**
    - a close request for one window leaves the other open;
    - quit closes every window;
    - a closed window stays closed.
- **`rhi.swapchain`:**
  - three frames while minimised present nothing and submit nothing (no GPU frame times);
  - after a restore, frames present again without a rebuild.
- **`lifecycle.restart` and `lifecycle.restart_vulkan`:** as above. Under ASan/UBSan, LeakSanitizer also checks the restarts for leaks.
- **Deliberate breaks:** to be recorded.
- **Local results:** to be recorded.
- **CI:** blocked for the whole account since 2026-11-05 (see ADR 0033); to be recorded.
