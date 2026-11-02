# ADR 0012 — Swapchain, frames in flight and a headless Vulkan window

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 4 (branch `claude/vulkan-rhi-4`), stacked on RHI PR 3.
Supersedes / superseded by: none. Builds on [ADR 0008](0008-window-input.md), [ADR 0010](0010-rhi-device.md) and [ADR 0011](0011-offscreen-render-target.md).

## Context

The interactive path must present to the SDL window with the same rendering path as the headless one (ADR 0002). Hosted CI runners have no display and no GPU. SDL's `offscreen` video driver can host a Vulkan surface through `VK_EXT_headless_surface`, which Mesa lavapipe implements, so the whole acquire, render and present cycle can run in CI.

## Options

- Test the swapchain only on a developer desktop. CI would only compile it.
- Use the offscreen driver and the headless surface extension in CI, and keep the desktop as the visual check.
- Block on every frame (as the one-shot submit does). Simple but serialises CPU and GPU.

## Decision

Proposed, not accepted by the owner:

- `WindowConfig::driver` gains `VideoDriver::Offscreen` (SDL `offscreen`): headless, but it can host a Vulkan surface. `Dummy` stays the non-Vulkan headless driver. `NativeWindowKind::Offscreen` reports it.
- `rhi::Swapchain` creates the window surface, picks an 8-bit UNORM format (B8G8R8A8, then R8G8B8A8, sRGB-nonlinear colour space; sRGB/HDR output waits for the colour-pipeline decision), and picks FIFO for vsync, or mailbox, then immediate, then FIFO without vsync.
- **Two frames in flight.** Each frame slot has its own command pool, command buffer, fence and image-available semaphore. Each swapchain image has its own render-finished semaphore, because present may still wait on it when the slot comes round again.
- Each frame clears through the shared dynamic-rendering clear pass (`detail::record_clear_pass`, also used by `RenderTarget`) with synchronization2 barriers, submits with `vkQueueSubmit2` and presents.
- The swapchain is rebuilt when the window's pixel size differs from its extent, or when the driver reports out-of-date or suboptimal. A zero-sized (minimised) window releases the swapchain and skips frames. Rebuilds wait for the device to go idle; that is acceptable for resizes.
- Presentation uses the device's single graphics queue. A device whose graphics queue cannot present is `NoSuitableDevice`; separate present queues wait for hardware that needs them.
- `lykoware_sandbox --window --vulkan` presents a cycling clear colour on the desktop. `--offscreen` is the headless form and needs `--frames`. Without a usable Vulkan device it exits 3, the code `lyko` already uses for "unsupported", and it exits 1 on any validation error.

## Contracts and compatibility

Ownership, lifetime, threading, resize and failure are on the declarations in [engine/include/lykoware/rhi/swapchain.hpp](../../engine/include/lykoware/rhi/swapchain.hpp). The Window, Device and Instance must outlive the Swapchain. Adding an enumerator to `VideoDriver` and `NativeWindowKind` is source-compatible for callers that do not switch exhaustively over them.

## Consequences

CI now runs the real present path, with the validation layer checking synchronisation. CI cannot show what reaches a real display, colour accuracy, vsync timing or tearing; those need the owner's desktop and later hardware profiles. Everything drawn is still a clear; pipelines and shaders come next, after the shader-compiler licence review.

## Verification

- `rhi.swapchain`, on an offscreen 64×48 window:
  - checks that a device without `VK_KHR_swapchain` is refused;
  - creates a vsync swapchain and presents five frames;
  - resizes the window to 80×60 and checks that the swapchain rebuilds to that size and presents three more frames;
  - creates a non-vsync swapchain and presents four frames;
  - requires zero validation errors.
- `sandbox.vulkan_window` runs `lykoware_sandbox --window --offscreen --vulkan --frames 3`. `foundation.smoke` checks that `--dummy --vulkan` and `--offscreen` without `--frames` are refused.
- `window.lifecycle` opens, resizes and pumps an offscreen window.
- Local results (Linux, GCC 13, Mesa 25.2 lavapipe, validation layer on): 19/19 tests in the Debug, ASan/UBSan and clang-tidy presets. With drivers hidden, the sandbox exited 3.
- Not verified: a visible desktop window on Windows or Linux with a real GPU. That needs the owner to run `lykoware_sandbox --window --vulkan`.
- CI results are recorded in STATUS when the PR runs.
