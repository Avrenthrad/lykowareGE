# ADR 0009 — Vulkan loading and the RHI instance

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 1 (branch `claude/vulkan-rhi-1`).
Supersedes / superseded by: none. Implements part of [ADR 0002](0002-graphics.md), which stays accepted and unchanged.

## Context

ADR 0002 accepts Vulkan 1.3 behind a Lykoware-owned RHI, with Vulkan-Headers, volk and Vulkan Memory Allocator vendored when a caller needs them. The first caller is the RHI instance. Hosted CI runners have no GPU, and a Windows runner may have no Vulkan loader at all, so the build must not require Vulkan to link or run. A test that cannot run must not report a pass.

## Options

- Link the Vulkan loader at build time. Simple, but every executable then fails to start on a machine without Vulkan, including CI runners and tools that never render.
- Load the Vulkan loader at run time through volk. One process-wide function table; no link-time dependency.
- Write a Lykoware loader shim. Duplicates volk with no benefit.

Vulkan-Headers ships C headers and the Vulkan-Hpp C++ bindings. The RHI exposes its own types, so the C++ bindings are not needed.

## Decision

Proposed, not accepted by the owner:

- Vendor the Vulkan-Headers C headers and volk at the same Vulkan SDK tag (`vulkan-sdk-1.4.363.0`), unmodified. Omit Vulkan-Hpp. Versions, commits, hashes and licences are in `third_party/vulkan-headers/PROVENANCE.md` and `third_party/volk/PROVENANCE.md`. The headers are used under the MIT option of `Apache-2.0 OR MIT`; volk is MIT.
- Defer Vulkan Memory Allocator to the first RHI change that allocates GPU memory (offscreen render target), per the caller rule in ADR 0002.
- Load Vulkan at run time through volk. Compile with `VK_NO_PROTOTYPES`. Only `engine/src/rhi/` includes `volk.h`. Public RHI headers include no Vulkan headers.
- `lykoware::rhi::Instance` requires a loader reporting instance version 1.3 or later, allows one live instance per process (volk's table is global), and can enable `VK_LAYER_KHRONOS_validation` (off, if available, or required). Validation messages go to `lykoware::log` and are counted, so tests can assert zero validation errors.
- A GPU test that finds no Vulkan loader exits 77, which CTest reports as Skipped, never Passed.
- Linux CI installs the Ubuntu `libvulkan1` and `vulkan-validationlayers` packages so the instance test runs there. These are free distribution packages. A software driver for device-level tests (Mesa lavapipe) is evaluated in the next RHI PR.

## Contracts and compatibility

Ownership, lifetime, threading and failure are documented on the declarations in [engine/include/lykoware/rhi/instance.hpp](../../engine/include/lykoware/rhi/instance.hpp). `Instance::native_handle()` returns the `VkInstance` as `void*` for `Window::create_vulkan_surface` (ADR 0008). Failure reasons are the `RhiFailure` enumeration, which later RHI objects reuse.

## Consequences

The engine and every tool still build and start on a machine with no Vulkan installed. Only RHI calls fail, with `RhiFailure::NoLoader`. The one-instance rule matches the engine's single renderer; supporting several instances would need volk's per-instance tables and is not planned. macOS portability enumeration is not handled because Windows and Linux are the targets.

## Verification

- `rhi.instance` creates and destroys an instance, checks the version, refuses a second live instance, refuses an unknown extension by name, enables validation when installed (and checks `Required` fails with `MissingLayer` when it is not), and re-creates an instance after destruction.
- Local results (Linux, GCC 13, loader 1.3.275 with the Khronos validation layer): passed in the Debug, ASan/UBSan and clang-tidy presets. With the loader removed, the test exited 77 and CTest reported it as Skipped.
- CI results are recorded in STATUS when the PR runs. A Windows runner without a loader is expected to report Skipped.
