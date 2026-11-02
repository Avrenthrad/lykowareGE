# ADR 0011 — Offscreen render target, GPU memory and read-back

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 3 (branch `claude/vulkan-rhi-3`), stacked on RHI PR 2.
Supersedes / superseded by: none. Builds on [ADR 0002](0002-graphics.md), [ADR 0007](0007-image-harness.md), [ADR 0009](0009-vulkan-loading.md) and [ADR 0010](0010-rhi-device.md).

## Context

ADR 0002 requires headless and interactive rendering to share one RHI path, and ADR 0007 left a plug-in point for a real GPU capture. The first rendered pixels need GPU memory, a way to submit work, a render target, and a read-back into the image harness. Hosted CI has only the lavapipe CPU driver.

## Options

- Clear with `vkCmdClearColorImage`. Simple, but it is a transfer command, not the rendering path that draws will use.
- Clear through a dynamic-rendering pass (`vkCmdBeginRendering` with a load-op clear). The same attachment, layout and barrier path later draws use.
- Allocate memory by hand per resource, or use Vulkan Memory Allocator (named in ADR 0002).
- Commit golden PPM files for clears, or build the expected solid image in the test.

## Decision

Proposed, not accepted by the owner:

- Vendor Vulkan Memory Allocator **3.4.0**, unmodified, under `third_party/vma` (MIT). 3.5.0 was one week old at vendoring; see `third_party/vma/PROVENANCE.md`. VMA gets volk's `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` (`VMA_DYNAMIC_VULKAN_FUNCTIONS`); its implementation compiles once in `lykoware_vma` without Lykoware's warning flags.
- `Device` owns the allocator plus one command pool, command buffer and fence for blocking one-shot submissions (`vkQueueSubmit2`, ten-second fence timeout reported as an error). Frame pacing and multiple frames in flight come with the swapchain.
- `RenderTarget`: one `R8G8B8A8_UNORM` image (colour attachment and transfer source) and a persistently mapped host read-back buffer. `clear()` runs a dynamic-rendering pass whose only work is a load-op clear. `read_back()` copies to the buffer with synchronization2 barriers, invalidates, and converts to the harness's RGB8 `Image` (alpha dropped, top-left origin). Reading back before any render is `RhiFailure::InvalidArgument`. Extents are 1..4096, the harness allocation guard, and within the device limit.
- `RenderTargetCapture` implements `lykoware::ImageCapture`, which is the plug-in ADR 0007 described.
- Tests build expected solid images in code from k/255 clear colours, which convert exactly, and compare at tolerance 0 through the harness. On a mismatch they write the actual image and the harness diff under the build directory. Golden files are added when content (geometry, shading) cannot be described in code; nothing regenerable is committed.
- `RhiFailure` gains `InvalidArgument`. `lyko screenshot` still exits 3: there is no scene to capture.

## Contracts and compatibility

Ownership, lifetime, threading and failure are on the declarations in [engine/include/lykoware/rhi/render_target.hpp](../../engine/include/lykoware/rhi/render_target.hpp) and [engine/include/lykoware/rhi/device.hpp](../../engine/include/lykoware/rhi/device.hpp). Internal RHI code reaches the device's Vulkan handles, allocator and submit helper through `engine/src/rhi/device_access.hpp`, which is not public.

## Consequences

The engine renders headless and the result can be compared pixel-for-pixel in CI. Every submission blocks until done, which is fine for capture and tests but not for frames; the swapchain PR adds frames in flight. The target format is fixed at RGBA8 UNORM; sRGB and HDR targets need a later decision with the renderer's colour pipeline.

## Verification

- `rhi.render_target` clears a 16×16 and a 17×9 target twice each, the second time read through `RenderTargetCapture`, and requires exact solid images. It also checks that a read-back before any render is refused and leaves the image empty, that bad extents are refused, and that there are zero validation errors when the layer is installed. It exits 77 (CTest Skipped) without Vulkan.
- Mutation check: swapping the red and blue channels in `read_back` made all four comparisons fail, with actual and diff images written. Restored afterwards.
- Local results (Linux, GCC 13, Mesa 25.2 lavapipe, validation layer on): 17/17 in the Debug, ASan/UBSan and clang-tidy presets. With drivers hidden it exited 77.
- CI results are recorded in STATUS when the PR runs.
