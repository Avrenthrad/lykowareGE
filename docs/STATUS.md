# Lykoware status

Records what is implemented and verified on `main`. Plans live in [ROADMAP.md](ROADMAP.md); task ownership in [WORK_BOARD.md](WORK_BOARD.md). Older per-branch handoffs are in the merged pull requests and git history.

## Decisions

| ADR | Status | Subject |
| --- | --- | --- |
| [0001](adr/0001-language.md) | Accepted | C++20 and CMake 3.25+ |
| [0002](adr/0002-graphics.md) | Accepted | Vulkan 1.3 from C++ behind a Lykoware-owned RHI. No graphics dependency vendored yet. |
| [0003](adr/0003-physics.md) | Proposed | Jolt, called natively from C++ if adopted. Owner has not decided. |
| [0004](adr/0004-data-formats.md) | Accepted | Canonical JSON for machine data, TOML for hand-edited config, glTF 2.0 meshes. No parser vendored yet. |
| [0005](adr/0005-native-cli.md)–[0007](adr/0007-image-harness.md) | Accepted implementation | CLI, CPU trace, GPU-free image harness |
| [0008](adr/0008-window-input.md) | Proposed | SDL 3.4.18 behind the window and input API. Merged and working; owner has not accepted it. |
| [0009](adr/0009-vulkan-loading.md) | Proposed (in review) | Vulkan-Headers and volk, Vulkan loaded at run time, RHI instance, GPU tests skip rather than pass without a loader. |
| [0010](adr/0010-rhi-device.md) | Proposed (in review) | Device requirements (Vulkan 1.3, dynamic rendering, synchronization2), selection ranking, one graphics-and-compute queue. |
| [0011](adr/0011-offscreen-render-target.md) | Proposed (in review) | VMA 3.4.0, offscreen RGBA8 render target, dynamic-rendering clear, read-back into the image harness. |
| [0012](adr/0012-swapchain.md) | Proposed (in review) | Swapchain with two frames in flight and resize rebuilds; `VideoDriver::Offscreen` headless Vulkan window; sandbox `--vulkan`. |
| [0013](adr/0013-shader-compiler.md) | **Accepted by owner 2026-11-03: Slang**, conditional on the build spike | Slang built without `slang-glslang` (avoids glslang's GPL-with-Bison-exception parser and NVIDIA AML preprocessor), plus SPIRV-Tools for validation. Owner accepted the one Unlicense file in Slang core. |

## Verified on main

All merges below passed the five CI jobs: Windows x64 Debug and Release, Linux Debug, Linux ASan/UBSan, and Linux clang-tidy 18.

| Area | Delivery | CI on main |
| --- | --- | --- |
| Module 01: engine library, console sandbox, logging | [af55b9b](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f) | [40265394221](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221) |
| `lyko build` / `lyko test`, warnings-as-errors, sanitizer and clang-tidy jobs | [PR #3](https://github.com/Avrenthrad/lykowareGE/pull/3) | [41325853657](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325853657) |
| Brand logo vector set under `assets/brand/` | [PR #2](https://github.com/Avrenthrad/lykowareGE/pull/2), merge d060469 | [41458565216](https://github.com/Avrenthrad/lykowareGE/actions/runs/41458565216) |
| ADR 0002 and 0004 acceptance; brief corrections | [PR #4](https://github.com/Avrenthrad/lykowareGE/pull/4) content, merged into `main` through the #5–#8 stack | as below |
| `lyko run`; `screenshot` and `run --scene` exit 3 | [PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), merge 8c7a8dd | [41458573116](https://github.com/Avrenthrad/lykowareGE/actions/runs/41458573116) |
| `CpuTimer`, `CpuTrace`, `lyko trace --frames N`, provisional hosted CPU budgets | [PR #6](https://github.com/Avrenthrad/lykowareGE/pull/6), merge 6dc9595 | [41458615375](https://github.com/Avrenthrad/lykowareGE/actions/runs/41458615375) |
| GPU-free image harness: `Image`, PPM P6, tolerance compare, diff, goldens | [PR #7](https://github.com/Avrenthrad/lykowareGE/pull/7), merge d4daed4 | [41458677412](https://github.com/Avrenthrad/lykowareGE/actions/runs/41458677412) |
| Module 02: `Window` and `InputState` over vendored SDL 3.4.18; dummy-driver tests; Vulkan surface hook without linking Vulkan | [PR #8](https://github.com/Avrenthrad/lykowareGE/pull/8), merge 5be6053 | [41458695818](https://github.com/Avrenthrad/lykowareGE/actions/runs/41458695818) |
| Window fixes: per-window event routing; a remaining gamepad takes over when the active one disconnects | [PR #9](https://github.com/Avrenthrad/lykowareGE/pull/9), merge b907aa7 | [41460782212](https://github.com/Avrenthrad/lykowareGE/actions/runs/41460782212) |

CTest on `main` (13 tests): `trace.api`, `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `input.mapping`, `window.lifecycle`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `foundation.smoke`, `cli.commands`, `cli.integration`.

## Current development task

Vulkan RHI bring-up, PR 1 of 5. Owner: Claude. Branch `claude/vulkan-rhi-1`, stacked on `claude/determined-dirac-qq42z3` ([PR #10](https://github.com/Avrenthrad/lykowareGE/pull/10), docs). Not merged.

- Vendored, unmodified: Vulkan-Headers C headers and volk, both at `vulkan-sdk-1.4.363.0`. Provenance, commits, hashes and licences in `third_party/vulkan-headers/PROVENANCE.md` and `third_party/volk/PROVENANCE.md`. Vulkan Memory Allocator is deferred to the first GPU allocation (RHI PR 3).
- `lykoware::rhi::Instance` (`engine/include/lykoware/rhi/instance.hpp`): loads Vulkan at run time through volk, requires loader version 1.3+, one instance per process, optional Khronos validation with messages logged and counted. No Vulkan types in public headers. ADR 0009, Proposed.
- New tests `rhi.instance` and `rhi.instance.no_driver`. Exit 77 without a loader or driver, which CTest reports as Skipped.
- Linux CI installs `libvulkan1`, `mesa-vulkan-drivers` (lavapipe CPU driver) and `vulkan-validationlayers` (free Ubuntu packages) so the tests run there.
- Local checks (Linux, GCC 13, loader 1.3.275, lavapipe, validation layer): Debug, ASan/UBSan and clang-tidy presets passed all 15 tests. With the loader removed, `rhi.instance` reported Skipped; with drivers hidden, it reported Skipped with `NoDriver`.
- CI: the first run failed `rhi.instance` on Linux (loader but no driver gave `VK_ERROR_INCOMPATIBLE_DRIVER`). Fixed by the `NoDriver` classification and installing lavapipe. [run 41473805033](https://github.com/Avrenthrad/lykowareGE/actions/runs/41473805033) on code commit 9c8c7c3 passed all five jobs: on Linux, `rhi.instance` ran on lavapipe and passed in Debug, ASan/UBSan and clang-tidy, and `rhi.instance.no_driver` passed; on Windows (no Vulkan on hosted runners) both reported Skipped.

### RHI PR 2 of 5 (stacked on PR 1)

Owner: Claude. Branch `claude/vulkan-rhi-2`, stacked on `claude/vulkan-rhi-1`. Not merged.

- `lykoware::rhi::Device` and `enumerate_devices` (`engine/include/lykoware/rhi/device.hpp`): requires Vulkan 1.3, a graphics-and-compute queue family, `dynamicRendering` and `synchronization2`; ranks discrete > integrated > virtual > other > CPU, or CPU first with `DevicePreference::Software`; optional name filter; every rejection explained. One device and one queue; one Device per process. ADR 0010, Proposed.
- Internal helpers shared by the RHI sources moved to `engine/src/rhi/common.*`. Destroying an Instance while a Device lives is logged as an error.
- New test `rhi.device` (Skipped without Vulkan).
- Local checks (Linux, GCC 13, lavapipe "llvmpipe", Vulkan 1.4.318, validation on): Debug, ASan/UBSan and clang-tidy presets passed all 16 tests with zero validation errors. With drivers hidden, `rhi.device` reported Skipped.
- CI: [run 41475053247](https://github.com/Avrenthrad/lykowareGE/actions/runs/41475053247) on code commit 1047e92 passed all five jobs. On Linux, `rhi.device` ran on lavapipe and passed in Debug, ASan/UBSan and clang-tidy; Windows hosted runners reported the RHI tests as Skipped.

### RHI PR 3 of 5 (stacked on PR 2)

Owner: Claude. Branch `claude/vulkan-rhi-3`, stacked on `claude/vulkan-rhi-2`. Not merged.

- Vendored, unmodified: Vulkan Memory Allocator 3.4.0 (MIT) under `third_party/vma`. 3.5.0 was one week old, so the previous release is pinned; see its PROVENANCE.
- `Device` now owns the VMA allocator and a blocking one-shot submit (command pool, buffer, fence; `vkQueueSubmit2`).
- `lykoware::rhi::RenderTarget` (`engine/include/lykoware/rhi/render_target.hpp`): RGBA8 UNORM offscreen image, dynamic-rendering load-op clear, synchronization2 read-back into the harness `Image`. `RenderTargetCapture` implements `ImageCapture`. ADR 0011, Proposed. First rendered pixels.
- New test `rhi.render_target`: exact solid-colour compares (expected images built in code, no committed goldens), actual and diff PPMs written under the build directory on mismatch. A deliberate red/blue swap made all four compares fail.
- Local checks (Linux, GCC 13, lavapipe, validation on): Debug, ASan/UBSan and clang-tidy presets passed all 17 tests with zero validation errors. With drivers hidden, `rhi.render_target` reported Skipped.
- CI: [run 41476543108](https://github.com/Avrenthrad/lykowareGE/actions/runs/41476543108) on code commit 87a8435 passed all five jobs. On Linux, `rhi.render_target` rendered on lavapipe and passed in Debug, ASan/UBSan and clang-tidy; Windows hosted runners reported the RHI tests as Skipped.

### RHI PR 4 of 5 (stacked on PR 3)

Owner: Claude. Branch `claude/vulkan-rhi-4`, stacked on `claude/vulkan-rhi-3`. Not merged.

- `lykoware::rhi::Swapchain` (`engine/include/lykoware/rhi/swapchain.hpp`): window surface, UNORM format, FIFO or mailbox/immediate, two frames in flight, per-image present semaphores, rebuild on resize/out-of-date/suboptimal, minimised windows skip frames. Uses the same dynamic-rendering clear pass as `RenderTarget`. ADR 0012, Proposed.
- `VideoDriver::Offscreen`: SDL's headless driver that can host a Vulkan surface (`VK_EXT_headless_surface`, supported by lavapipe), so CI runs the real present path.
- `lykoware_sandbox --window --vulkan` presents a cycling colour on the desktop; `--offscreen --frames N` is the headless form; exit 3 without a usable Vulkan device.
- New tests `rhi.swapchain` and `sandbox.vulkan_window`; `window.lifecycle` covers the offscreen driver; `foundation.smoke` covers the new option errors.
- Local checks (Linux, GCC 13, lavapipe, validation on): Debug, ASan/UBSan and clang-tidy presets passed all 19 tests with zero validation errors.
- Not verified: a visible desktop window with a real GPU. Owner check: `lykoware_sandbox --window --vulkan`.
- CI: the first run ([run 41478183872](https://github.com/Avrenthrad/lykowareGE/actions/runs/41478183872)) failed both Windows builds with MSVC C4458 (a parameter named `extent` hid the member `extent`; GCC reports it only with `-Wshadow`). Fixed in f2ca3e9 after reproducing with `-Wshadow`; [run 41478643774](https://github.com/Avrenthrad/lykowareGE/actions/runs/41478643774) on f2ca3e9 then passed all five jobs. On Linux, `rhi.swapchain` and `sandbox.vulkan_window` ran on lavapipe and passed in Debug, ASan/UBSan and clang-tidy; Windows reported the Vulkan tests as Skipped.

## Next

Owner order, set 2026-11-02. It replaces the earlier "clocks first" order.

1. Vulkan RHI bring-up, in small stacked PRs. Vendor pinned Vulkan-Headers, volk and Vulkan Memory Allocator with exact licences and versions. Create the instance and device, a swapchain on the SDL window, and a clear screen. Add headless offscreen capture through the same RHI, feeding the image harness. Compile SPIR-V ahead of time; review the shader-compiler licence first. Evaluate Mesa lavapipe for graphics CI before claiming graphics CI.
2. Phase 1 clocks and fixed-step update loop.
3. Decide Jolt (ADR 0003) before any physics.
4. Agree hardware profiles (RTX 4070 desktop, MSI Claw), multiplayer intent, world scope and budgets. Owner's current leaning on multiplayer: co-op later or MMO, undecided. This is not a commitment.

## Known issues and limitations

- No renderer, ECS, scene loader, physics, audio, animation, world streaming or inference. The RHI so far (in review) creates a Vulkan instance, a device with one queue, an offscreen render target it can clear and read back, and a swapchain that presents clears to a window; no pipelines or shaders, so nothing but clears is drawn.
- GPU tests skip (exit 77, CTest "Skipped") where no Vulkan loader or driver is installed. Hosted Windows runners may skip them; a skip is not a pass.
- `lyko run` launches the console sandbox. `lyko screenshot` and `lyko run --scene` print `unsupported: no renderer` and exit 3. `lyko trace --frames N` is a CPU trace of the sandbox loop, not a GPU capture.
- The image harness compares a software test pattern with committed PPM fixtures and, in review, Vulkan clears read back from a render target. Neither is a scene screenshot or the image/performance gate; `lyko screenshot` still exits 3.
- Provisional CPU budgets apply only to the hosted windows-2022 and ubuntu-24.04 runners. They are not an RTX 4070 or MSI Claw gate.
- Window: a real Win32/X11/Wayland desktop window, focus changes and a real Vulkan surface are untested in CI. A Linux desktop window needs X11 or Wayland headers at configure time. A newly adopted gamepad starts with released buttons and centred axes until it reports a change.
- Vendored runtime sources: SDL 3.4.18 (HIDAPI inside it under the BSD-style licence, not GPL); in review, Vulkan-Headers C headers (MIT option), volk (MIT) and Vulkan Memory Allocator 3.4.0 (MIT). See [third_party/README.md](../third_party/README.md).
- Sanitizers are Linux-only. ThreadSanitizer is deferred until there are concurrent engine systems.
- The CLI is a developer process wrapper, not the future command bus. See [CLI.md](CLI.md).
- Pax review, patent review (including US 10,926,179 for Living Memory) and legal clearance remain required before anything is public. None has been done.
- GitHub artifact storage is exhausted. Uploads stay opt-in; builds and tests do not depend on them. No storage was bought and no paid operation was run.

## Handoff

- State: `main` at b907aa7 contains PRs #2 and #4–#9. CI is green on all five jobs ([run 41460782212](https://github.com/Avrenthrad/lykowareGE/actions/runs/41460782212)).
- Open decisions for the owner: ADR 0003 (Jolt), ADR 0008 (SDL3), hardware profiles, multiplayer, world scope and budgets.
- In review: RHI PRs 1–4 (`claude/vulkan-rhi-1` to `-4`). Owner chose Slang (ADR 0013, 2026-11-03). The build spike passed on Linux: offline minimal build (732 s on 4 cores), SPIR-V via `slangc -O0` plus our own `spirv-opt` and `spirv-val`, a triangle drawn on lavapipe with zero validation errors once `shaderDrawParameters` is enabled, and a clean licence inventory. Next concrete action: vendor Slang and SPIRV-Tools, compile shaders at build time, and add a triangle pipeline test. That waits on the owner's answer about caching the compiler in CI.
- Owner check wanted: run `lykoware_sandbox --window --vulkan` on the RTX 4070 desktop and the MSI Claw; a window cycling through colours is expected.
- Checklist: no box ticked by this update. Phase 1 and the image/performance gate stay open.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and the [ADRs](adr/README.md). Check latest `main` and open PRs before starting. Update this file with exact commits, checks, failures and next actions.
