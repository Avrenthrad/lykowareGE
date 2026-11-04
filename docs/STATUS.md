# Lykoware status

Records what is implemented and verified on `main`. Plans live in [ROADMAP.md](ROADMAP.md); task ownership in [WORK_BOARD.md](WORK_BOARD.md). Older per-branch handoffs are in the merged pull requests and git history.

## Decisions

| ADR | Status | Subject |
| --- | --- | --- |
| [0001](adr/0001-language.md) | Accepted | C++20 and CMake 3.25+ |
| [0002](adr/0002-graphics.md) | Accepted | Vulkan 1.3 from C++ behind a Lykoware-owned RHI. Vulkan-Headers, volk and VMA are vendored (ADRs 0009–0011). |
| [0003](adr/0003-physics.md) | **Accepted by owner 2026-11-04** | Jolt Physics v5.6.0 (MIT), natively from C++ behind a Lykoware API. The two *Numerical Recipes*-derived functions are replaced by our own; cross-platform determinism is on; float precision for now. Not yet vendored: the spike follows. |
| [0004](adr/0004-data-formats.md) | Accepted | Canonical JSON for machine data, TOML for hand-edited config, glTF 2.0 meshes. No parser vendored yet. |
| [0005](adr/0005-native-cli.md)–[0007](adr/0007-image-harness.md) | Accepted implementation | CLI, CPU trace, GPU-free image harness |
| [0008](adr/0008-window-input.md) | Proposed | SDL 3.4.18 behind the window and input API. Merged and working; owner has not accepted it. |
| [0009](adr/0009-vulkan-loading.md) | Proposed (merged; owner has not accepted) | Vulkan-Headers and volk, Vulkan loaded at run time, RHI instance, GPU tests skip rather than pass without a loader. |
| [0010](adr/0010-rhi-device.md) | Proposed (merged; owner has not accepted) | Device requirements (Vulkan 1.3, dynamic rendering, synchronization2), selection ranking, one graphics-and-compute queue. |
| [0011](adr/0011-offscreen-render-target.md) | Proposed (merged; owner has not accepted) | VMA 3.4.0, offscreen RGBA8 render target, dynamic-rendering clear, read-back into the image harness. |
| [0012](adr/0012-swapchain.md) | Proposed (merged; owner has not accepted) | Swapchain with two frames in flight and resize rebuilds; `VideoDriver::Offscreen` headless Vulkan window; sandbox `--vulkan`. |
| [0013](adr/0013-shader-compiler.md) | **Accepted by owner 2026-11-03: Slang**; the build spike passed | Slang built without `slang-glslang` (avoids glslang's GPL-with-Bison-exception parser and NVIDIA AML preprocessor), plus SPIRV-Tools for validation. Owner accepted the one Unlicense file in Slang core. |
| [0014](adr/0014-shader-build.md) | Proposed (merged; owner has not accepted) | Host tools built once from vendored source into a cache; `slangc` → `spirv-opt` → `spirv-val` at build time; CI Actions cache. |
| [0015](adr/0015-graphics-pipeline.md) | Proposed (merged; owner has not accepted) | `rhi::Shader`, `rhi::GraphicsPipeline`, `RenderTarget::draw`, `shaderDrawParameters`; sanitizer builds keep Vulkan drivers loaded. |
| [0016](adr/0016-runtime-shader-files.md) | Proposed (merged; owner has not accepted) | Programs load their compiled shaders from a `shaders` folder beside the executable; the build and `cmake --install` deploy them there. |
| [0017](adr/0017-clocks-fixed-step.md) | Proposed (merged; owner has not accepted) | Fixed-step simulation clock: 60 ticks a second by default, exact integer accumulator, interpolation alpha for drawing, 250 ms and 8 ticks per frame limits. |
| [0018](adr/0018-jolt-integration.md) | **Accepted by owner 2026-11-04** (including the AVX2 CPU requirement) | Jolt built from vendored source with options forced; cross-platform determinism verified by a recorded state hash in every CI job; requires an AVX2 CPU. |

## Verified on main

The five CI jobs are Windows x64 Debug and Release, Linux Debug, Linux ASan/UBSan, and Linux clang-tidy 18. Every merge up to #9 passed all five on `main`. #10–#16 each passed all five as pull requests (runs below). They were merged on 2026-11-03 at the owner's request, with `[skip ci]` on #10–#15 so that `main` runs CI once, on the #16 merge, for the whole stack.

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
| STATUS and WORK_BOARD for the RHI-first plan (docs) | [PR #10](https://github.com/Avrenthrad/lykowareGE/pull/10), merge 392c030 | covered by the #16 run |
| RHI 1: vendored Vulkan-Headers and volk (`vulkan-sdk-1.4.363.0`); `rhi::Instance` with run-time loading, typed failures (`NoLoader`, `NoDriver`, …) and counted validation messages; lavapipe in Linux CI | [PR #11](https://github.com/Avrenthrad/lykowareGE/pull/11), merge 1ac4e0c | PR run [41473805033](https://github.com/Avrenthrad/lykowareGE/actions/runs/41473805033); covered by the #16 run |
| RHI 2: `rhi::Device` and `enumerate_devices`; requirements, ranking, rejection reasons; one queue, one Device per process | [PR #12](https://github.com/Avrenthrad/lykowareGE/pull/12), merge 94b6ae0 | PR run [41475053247](https://github.com/Avrenthrad/lykowareGE/actions/runs/41475053247); covered by the #16 run |
| RHI 3: vendored VMA 3.4.0; `rhi::RenderTarget` clear and read-back; `RenderTargetCapture` feeds the image harness (first rendered pixels) | [PR #13](https://github.com/Avrenthrad/lykowareGE/pull/13), merge 8d33414 | PR run [41476543108](https://github.com/Avrenthrad/lykowareGE/actions/runs/41476543108); covered by the #16 run |
| RHI 4: `rhi::Swapchain`, two frames in flight, resize rebuilds; `VideoDriver::Offscreen` so CI runs the present path; sandbox `--vulkan` | [PR #14](https://github.com/Avrenthrad/lykowareGE/pull/14), merge 528304a | PR run [41478643774](https://github.com/Avrenthrad/lykowareGE/actions/runs/41478643774); covered by the #16 run |
| ADR 0013 shader-compiler licence review (docs) | [PR #15](https://github.com/Avrenthrad/lykowareGE/pull/15), merge 09ebf36 | covered by the #16 run |
| RHI 5a: vendored Slang v2026.18.3, SPIRV-Tools v2026.4 and SPIRV-Headers; host tools built once into `.lykoware-tools/` and cached in CI; every shader compiled, optimised and validated at build time | [PR #16](https://github.com/Avrenthrad/lykowareGE/pull/16), merge c60264d | PR run [41626480873](https://github.com/Avrenthrad/lykowareGE/actions/runs/41626480873); `main` run [41735089452](https://github.com/Avrenthrad/lykowareGE/actions/runs/41735089452) passed all five jobs (cold tool cache, now saved for `main`) |
| RHI 5b: `rhi::Shader`, `rhi::GraphicsPipeline`, `RenderTarget::draw`; devices enable `shaderDrawParameters`; the first triangle drawn offscreen and checked by the image harness | [PR #17](https://github.com/Avrenthrad/lykowareGE/pull/17), merge 5ffb8ac | PR run [41735418191](https://github.com/Avrenthrad/lykowareGE/actions/runs/41735418191); `main` run [41740344136](https://github.com/Avrenthrad/lykowareGE/actions/runs/41740344136) passed all five jobs |
| RHI 5c: the triangle drawn in the sandbox window; programs load shaders from a `shaders` folder beside the executable, deployed by the build and install | [PR #18](https://github.com/Avrenthrad/lykowareGE/pull/18), merge 16101ec | PR run [41747802339](https://github.com/Avrenthrad/lykowareGE/actions/runs/41747802339); `main` run [41758233376](https://github.com/Avrenthrad/lykowareGE/actions/runs/41758233376) passed all five jobs |
| Phase 1 clocks: `FixedStep` simulation clock (60 Hz default, exact integer accumulator, interpolation alpha, per-frame limits) and `FrameClock`; the sandbox animates per tick | [PR #19](https://github.com/Avrenthrad/lykowareGE/pull/19), merge ff2a1ea | PR run [41758959579](https://github.com/Avrenthrad/lykowareGE/actions/runs/41758959579) passed all five jobs (`clock.fixed_step` on MSVC, GCC, Clang-tidy and sanitizers); `main` run [41759429068](https://github.com/Avrenthrad/lykowareGE/actions/runs/41759429068) passed all five jobs |
| Jolt licence and fit review; owner accepted ADR 0003 (docs) | [PR #20](https://github.com/Avrenthrad/lykowareGE/pull/20), merge 8089b22 | `main` run [41760436240](https://github.com/Avrenthrad/lykowareGE/actions/runs/41760436240) passed all five jobs |

CTest on `main` (24 tests): `trace.api`, `clock.fixed_step`, `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `input.mapping`, `window.lifecycle`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `foundation.smoke`, `cli.commands`, `cli.integration`, `rhi.instance`, `rhi.instance.no_driver`, `rhi.device`, `rhi.render_target`, `rhi.swapchain`, `sandbox.vulkan_window`, `shaders.spirv_valid`, `shaders.spirv_valid_fragment`, `rhi.pipeline`, `sandbox.missing_shaders`. The `rhi.*` and `sandbox.vulkan_window` tests run on lavapipe in Linux CI and report Skipped on the Windows runners, which have no Vulkan.

Per-PR detail (local checks, failures found and fixed, mutation checks) is in each pull request and in ADRs 0009–0017.

## Current development task

### Physics spike, PR 1: vendored Jolt, local patch, determinism check

Owner: Claude. Branch `claude/physics-spike`, based on `main` at 8089b22. In review.

- **Vendored:** Jolt Physics v5.6.0 under `third_party/jolt` (5.7 MB), with provenance and hashes. The two *Numerical Recipes*-derived headers are replaced by Lykoware implementations (cyclic Jacobi eigenvalues; Gauss–Jordan with row pivoting), as the owner chose.
- **Build:** `cmake/VendorJolt.cmake` adds upstream's CMake as a system subdirectory with options forced (ADR 0018, Proposed):
  - cross-platform determinism on, float precision;
  - a static library with the engine's MSVC runtime, no link-time optimisation, RTTI on;
  - vendored warnings not treated as errors;
  - no debug renderer, profiler, object streams or GPU compute;
  - sanitizer builds compile Jolt with ASan/UBSan.
- **Hardware:** Jolt's AVX2 instruction-set flags are kept, so the engine now needs an AVX2 CPU. The MSI Claw and current desktops qualify (owner attention, ADR 0018).
- **New tests** (call Jolt directly; the `lykoware::physics` wrapper is the next PR):
  - `physics.jolt_math`: both replacement functions against known answers, and inertia decomposition;
  - `physics.jolt_determinism`: a 64-body scene for 300 ticks at 60 Hz. Two runs match, save and restore reproduces the state, and the final state hash must equal `0x17e8264fc6fa9cd7` in every build.
- **Local checks:** GCC 13 and Clang 18, Debug and Release, all produce that hash. 26/26 tests pass in the Debug, ASan/UBSan (with Jolt itself instrumented) and clang-tidy presets.
- **Deliberate breaks,** each caught: turning determinism off (different hash), a sign error in the eigen solver, removing row swaps from Gauss–Jordan.
- **CI:** [run 41762141044](https://github.com/Avrenthrad/lykowareGE/actions/runs/41762141044) on a443f2a passed all five jobs. **`physics.jolt_determinism` reproduced `0x17e8264fc6fa9cd7` on Windows MSVC Debug and Release** as well as on Linux GCC (Debug, ASan/UBSan, clang-tidy build). Cross-platform determinism holds for this scene across compilers, operating systems and build types.

## Next

Owner order, set 2026-11-02. It replaces the earlier "clocks first" order.

1. Vulkan RHI bring-up: done (#11–#18). Instance, device, offscreen render target read back into the image harness, swapchain, build-time Slang shaders, graphics pipelines, and the triangle offscreen and in the sandbox window.
2. Phase 1 clocks and fixed-step update loop: done (#19, ADR 0017).
3. Physics: Jolt accepted (ADR 0003, 2026-11-04). Spike PR 1 (vendor, patch, build, determinism hash) is in review. Next: the `lykoware::physics` wrapper with stable body handles, per-tick stepping, ray casts and invalid-handle checks; then a character on a height field and a first body-count benchmark.
4. Agree hardware profiles (RTX 4070 desktop, MSI Claw), multiplayer intent, world scope and budgets. Owner's current leaning on multiplayer: co-op later or MMO, undecided. This is not a commitment.

## Known issues and limitations

- No renderer, ECS, scene loader, physics, audio, animation, world streaming or inference. The RHI on `main` creates a Vulkan instance, a device with one queue, an offscreen render target it can clear, draw into and read back, a swapchain, and graphics pipelines from Slang shaders compiled at build time; the sandbox window draws a test triangle. There are no vertex buffers, textures, depth or camera yet, and no swapchain read-back, so window pixels are not checked automatically.
- GPU tests skip (exit 77, CTest "Skipped") where no Vulkan loader or driver is installed. Hosted Windows runners may skip them; a skip is not a pass.
- `lyko run` launches the console sandbox. `lyko screenshot` and `lyko run --scene` print `unsupported: no renderer` and exit 3. `lyko trace --frames N` is a CPU trace of the sandbox loop, not a GPU capture.
- The image harness compares a software test pattern with committed PPM fixtures, and Vulkan clears read back from a render target. Neither is a scene screenshot or the image/performance gate; `lyko screenshot` still exits 3.
- Provisional CPU budgets apply only to the hosted windows-2022 and ubuntu-24.04 runners. They are not an RTX 4070 or MSI Claw gate.
- Window: a real Win32/X11/Wayland desktop window, focus changes and a real Vulkan surface are untested in CI. A Linux desktop window needs X11 or Wayland headers at configure time. A newly adopted gamepad starts with released buttons and centred axes until it reports a change.
- Vendored runtime sources: SDL 3.4.18 (HIDAPI inside it under the BSD-style licence, not GPL), Vulkan-Headers C headers (MIT option), volk (MIT) and Vulkan Memory Allocator 3.4.0 (MIT). Vendored build-time tools, not shipped: Slang (Apache-2.0 WITH LLVM-exception, one owner-accepted Unlicense file), SPIRV-Tools and SPIRV-Headers. See [third_party/README.md](../third_party/README.md).
- The first configure on a machine builds the shader tools, which takes about 15 extra minutes once per tool version. CI keeps them in the free Actions cache.
- Sanitizers are Linux-only. ThreadSanitizer is deferred until there are concurrent engine systems.
- The CLI is a developer process wrapper, not the future command bus. See [CLI.md](CLI.md).
- Pax review, patent review (including US 10,926,179 for Living Memory) and legal clearance remain required before anything is public. None has been done.
- GitHub artifact storage is exhausted. Uploads stay opt-in; builds and tests do not depend on them. No storage was bought and no paid operation was run.

## Handoff

- State: `main` at 8089b22 contains PRs #2, #4–#20, green on all five jobs ([run 41760436240](https://github.com/Avrenthrad/lykowareGE/actions/runs/41760436240)). The Vulkan RHI bring-up and Phase 1 clocks are complete, and Jolt is accepted.
- Open decisions for the owner: ADR 0003 (Jolt); accepting or rejecting ADRs 0008–0012 and 0014–0017 (merging did not accept them); hardware profiles, multiplayer, world scope and budgets.
- In review: physics spike PR 1 (`claude/physics-spike`, ADR 0018): vendored Jolt v5.6.0 with the owner's local patch, built in all five jobs, with a cross-platform determinism hash. Next concrete action after it merges: the `lykoware::physics` wrapper.
- Owner check wanted: run `lykoware_sandbox --window --vulkan` on the RTX 4070 desktop and the MSI Claw. With 5c, a red, green and blue triangle over a slowly changing background is expected.
- Checklist: no box ticked by this update. Phase 1 and the image/performance gate stay open.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and the [ADRs](adr/README.md). Check latest `main` and open PRs before starting. Update this file with exact commits, checks, failures and next actions.
