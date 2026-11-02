# ADR 0010 — RHI device selection and the logical device

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 2 (branch `claude/vulkan-rhi-2`), stacked on RHI PR 1.
Supersedes / superseded by: none. Builds on [ADR 0002](0002-graphics.md) and [ADR 0009](0009-vulkan-loading.md).

## Context

After an instance exists, the RHI must pick one physical device and create a logical device and queue. Targets range from an RTX 4070 desktop to an Intel Arc handheld (MSI Claw); CI has only Mesa lavapipe, a CPU implementation. Agents and tests need to see why a device was rejected, and CI needs to choose the CPU device on purpose.

## Options

- Take the first device the driver lists. Unpredictable on multi-GPU machines (an integrated GPU can come first).
- Score devices on many properties (memory size, limits). More tuning than needed before any renderer exists.
- Hard requirements, then a simple type ranking, with an explicit software preference and a name filter.

## Decision

Proposed, not accepted by the owner:

- A device is suitable when it supports Vulkan 1.3, has one queue family with graphics and compute, supports the 1.3 features `dynamicRendering` and `synchronization2`, and offers every requested device extension. Those two features are enabled on every device: the renderer will use dynamic rendering instead of render-pass objects, and synchronization2 barriers.
- Among suitable devices, `DevicePreference::HighPerformance` (default) ranks discrete, integrated, virtual, other, CPU. `DevicePreference::Software` puts CPU devices first, for deterministic tests. Ties keep the driver's order. `DeviceConfig::name_contains` restricts the choice by name.
- `enumerate_devices` reports every device with its suitability and the first failed requirement. A failed `Device::create` returns `RhiFailure::NoSuitableDevice` with that list in the message.
- One logical device with one graphics-and-compute queue. Dedicated transfer or async-compute queues wait for a measured need.
- One `Device` per process at a time, because volk keeps one device function table. A second create fails with `AlreadyExists`. Destroying the `Instance` while a `Device` is alive is logged as an error.

## Contracts and compatibility

Ownership, lifetime, threading and failure are documented on the declarations in [engine/include/lykoware/rhi/device.hpp](../../engine/include/lykoware/rhi/device.hpp). Native handles are opaque pointers for later RHI code. `RhiFailure` gains `NoSuitableDevice`.

## Consequences

Device choice is predictable and explainable. A machine whose only GPU lacks Vulkan 1.3, dynamic rendering or synchronization2 is unsupported; the MSI Claw (Intel Arc) and RTX 4070 drivers support all three, but that is unverified on the hardware until profiles are agreed. Multi-device rendering is not supported.

## Verification

- `rhi.device` lists devices, creates the default device and checks its info, handles and `wait_idle`, refuses a second live device, refuses an unmatched name filter and a missing extension with reasons, checks that `Software` picks a CPU device when one exists, re-creates a device, and requires zero validation errors when the validation layer is installed. It exits 77 (CTest Skipped) without a loader, driver or suitable device.
- Local results (Linux, GCC 13, Mesa 25.2 lavapipe "llvmpipe", Vulkan 1.4.318, validation layer on): passed in the Debug, ASan/UBSan and clang-tidy presets. With drivers hidden it exited 77.
- CI results are recorded in STATUS when the PR runs.
