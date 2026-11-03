# Lykoware status

Records what is implemented and verified on `main`. Plans live in [ROADMAP.md](ROADMAP.md); task ownership in [WORK_BOARD.md](WORK_BOARD.md). Older per-branch handoffs are in the merged pull requests and git history.

## Decisions

| ADR | Status | Subject |
| --- | --- | --- |
| [0001](adr/0001-language.md) | Accepted | C++20 and CMake 3.25+ |
| [0002](adr/0002-graphics.md) | Accepted | Vulkan 1.3 from C++ behind a Lykoware-owned RHI. Vulkan-Headers, volk and VMA are vendored (ADRs 0009–0011). |
| [0003](adr/0003-physics.md) | Proposed | Jolt, called natively from C++ if adopted. Owner has not decided. |
| [0004](adr/0004-data-formats.md) | Accepted | Canonical JSON for machine data, TOML for hand-edited config, glTF 2.0 meshes. No parser vendored yet. |
| [0005](adr/0005-native-cli.md)–[0007](adr/0007-image-harness.md) | Accepted implementation | CLI, CPU trace, GPU-free image harness |
| [0008](adr/0008-window-input.md) | Proposed | SDL 3.4.18 behind the window and input API. Merged and working; owner has not accepted it. |
| [0009](adr/0009-vulkan-loading.md) | Proposed (merged; owner has not accepted) | Vulkan-Headers and volk, Vulkan loaded at run time, RHI instance, GPU tests skip rather than pass without a loader. |
| [0010](adr/0010-rhi-device.md) | Proposed (merged; owner has not accepted) | Device requirements (Vulkan 1.3, dynamic rendering, synchronization2), selection ranking, one graphics-and-compute queue. |
| [0011](adr/0011-offscreen-render-target.md) | Proposed (merged; owner has not accepted) | VMA 3.4.0, offscreen RGBA8 render target, dynamic-rendering clear, read-back into the image harness. |
| [0012](adr/0012-swapchain.md) | Proposed (merged; owner has not accepted) | Swapchain with two frames in flight and resize rebuilds; `VideoDriver::Offscreen` headless Vulkan window; sandbox `--vulkan`. |
| [0013](adr/0013-shader-compiler.md) | **Accepted by owner 2026-11-03: Slang**; the build spike passed | Slang built without `slang-glslang` (avoids glslang's GPL-with-Bison-exception parser and NVIDIA AML preprocessor), plus SPIRV-Tools for validation. Owner accepted the one Unlicense file in Slang core. |
| [0014](adr/0014-shader-build.md) | Proposed (merged; owner has not accepted) | Host tools built once from vendored source into a cache; `slangc` → `spirv-opt` → `spirv-val` at build time; CI Actions cache. |
| [0015](adr/0015-graphics-pipeline.md) | Proposed (in review) | `rhi::Shader`, `rhi::GraphicsPipeline`, `RenderTarget::draw`, `shaderDrawParameters`; sanitizer builds keep Vulkan drivers loaded. |

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

CTest on `main` (21 tests): `trace.api`, `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `input.mapping`, `window.lifecycle`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `foundation.smoke`, `cli.commands`, `cli.integration`, `rhi.instance`, `rhi.instance.no_driver`, `rhi.device`, `rhi.render_target`, `rhi.swapchain`, `sandbox.vulkan_window`, `shaders.spirv_valid`, `shaders.spirv_valid_fragment`. The `rhi.*` and `sandbox.vulkan_window` tests run on lavapipe in Linux CI and report Skipped on the Windows runners, which have no Vulkan.

Per-PR detail (local checks, failures found and fixed, mutation checks) is in each pull request and in ADRs 0009–0014.

## Current development task

### RHI PR 5b: first draw

Owner: Claude. Branch `claude/vulkan-rhi-5b`, based on `main` at c60264d. In review.

- `rhi::Shader` (SPIR-V words or `.spv` file; header checked; entry point `main`) and `rhi::GraphicsPipeline`:
  - dynamic rendering, one RGBA8 or BGRA8 colour attachment;
  - triangle lists, no vertex buffers, no culling, blending or depth;
  - dynamic viewport and scissor.
- `RenderTarget::draw(clear, pipeline, vertex_count)` uses the same pass helper as `clear()` and the swapchain, and refuses a pipeline built for another colour format.
- Devices now require and enable Vulkan 1.1 `shaderDrawParameters` (Slang's `SV_VertexID`).
- ADR 0015, Proposed.
- New test `rhi.pipeline`: the Slang triangle compiled at build time is drawn and checked through the image harness (sampled regions, identical repeat draw), plus refusals.
- Sanitizer builds set `VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1` for the GPU tests. Without it, LeakSanitizer reported lavapipe's own LLVM-thread memory when the loader unloaded the driver; a deliberate leak in Lykoware code is still caught with it on.
- Local checks (Linux, GCC 13, lavapipe, validation on): 22/22 in the Debug, ASan/UBSan and clang-tidy presets. Deliberately removing the draw, not enabling `shaderDrawParameters`, and leaking a `Shader` were each caught.
- CI: [run 41735418191](https://github.com/Avrenthrad/lykowareGE/actions/runs/41735418191) on c4d2113 passed all five jobs. `rhi.pipeline` drew the triangle on lavapipe and passed in Linux Debug, ASan/UBSan and clang-tidy (22/22 each); Windows reported it Skipped, as designed.

## Next

Owner order, set 2026-11-02. It replaces the earlier "clocks first" order.

1. Finish the Vulkan RHI bring-up. Done on `main`: instance, device, offscreen clear and read-back into the image harness, swapchain on the SDL window, and shaders compiled at build time; lavapipe runs the GPU tests in Linux CI. Left: the first draw (5b, in review), then 5c, where the sandbox window draws the triangle and we decide how the runtime finds shader files.
2. Phase 1 clocks and fixed-step update loop.
3. Decide Jolt (ADR 0003) before any physics.
4. Agree hardware profiles (RTX 4070 desktop, MSI Claw), multiplayer intent, world scope and budgets. Owner's current leaning on multiplayer: co-op later or MMO, undecided. This is not a commitment.

## Known issues and limitations

- No renderer, ECS, scene loader, physics, audio, animation, world streaming or inference. The RHI on `main` creates a Vulkan instance, a device with one queue, an offscreen render target it can clear and read back, and a swapchain that presents clears to a window, and the build compiles Slang shaders to validated SPIR-V. Graphics pipelines and the first draw (an offscreen test triangle) are in review in 5b. Nothing is drawn to the window yet beyond clears.
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

- State: `main` at c60264d contains PRs #2, #4–#16. Every PR in #10–#16 passed all five jobs as a pull request; the `main` run for the stack ([run 41735089452](https://github.com/Avrenthrad/lykowareGE/actions/runs/41735089452)) passed all five jobs.
- Open decisions for the owner: ADR 0003 (Jolt); accepting or rejecting ADRs 0008–0012, 0014 and 0015 (merging did not accept them); hardware profiles, multiplayer, world scope and budgets.
- In review: RHI 5b (`claude/vulkan-rhi-5b`), graphics pipelines and the first triangle drawn offscreen and checked by the image harness. Next concrete action after it merges: RHI 5c, the sandbox window draws the triangle; decide how the runtime finds shader files.
- Owner check wanted: run `lykoware_sandbox --window --vulkan` on the RTX 4070 desktop and the MSI Claw; a window cycling through colours is expected.
- Checklist: no box ticked by this update. Phase 1 and the image/performance gate stay open.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and the [ADRs](adr/README.md). Check latest `main` and open PRs before starting. Update this file with exact commits, checks, failures and next actions.
