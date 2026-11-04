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
| [0020](adr/0020-physics-api.md) | **Accepted by owner 2026-11-04** | `lykoware::physics::World`: stable generation-checked body and character handles, box/sphere/capsule/height-field bodies, walking characters, per-tick stepping, ray casts, step memory that overflows to the heap, defined failure for misuse; no Jolt types in public headers. |
| [0021](adr/0021-product-targets.md) | **Accepted by owner 2026-11-04** | Desktop PC only; the MSI Claw is no longer a target. RTX 4070 recommended at 1440p and 60 FPS, with a weaker minimum (about RTX 3060 / RX 6600, set by measurement). 16 GB RAM minimum. Single-player first, built co-op-ready (deterministic, command-driven). One seamless world of 16 km or more; positions in double precision now. |
| [0022](adr/0022-double-precision-positions.md) | Proposed (merged; owner chose the direction in ADR 0021) | Jolt built with `DOUBLE_PRECISION` on; public physics positions are `double` (`DVec3`); kinematic bodies never sleep; the same scene 16 km and 64 km out matches the origin. |
| [0023](adr/0023-ecs.md) | Proposed (merged in #29 and #31; owner chose "write our own") | `lykoware::ecs::World`: generation-checked entities, packed per-type component storage, deterministic `each` iteration, structural changes refused during iteration; removal hooks, `ChangeBuffer`, physics bodies and characters on entities. |
| [0024](adr/0024-math.md) | Proposed (owner chose "write our own") | `lykoware::math`: vectors, quaternions, column-major matrices, right-handed +Y up, Vulkan reversed infinite-depth projection, camera-relative `relative_to`; physics value types are now its aliases. |
| [0025](adr/0025-rhi-meshes.md) | Proposed | RHI meshes: `rhi::Buffer` (vertex and 32-bit index data), pipelines with a position-normal vertex layout, reversed-depth test, back-face culling (counter-clockwise front faces) and up to 128 bytes of push constants; render targets with depth and `draw_meshes`. |
| [0026](adr/0026-renderer.md) | Proposed | The first renderer: `lykoware::Transform`, `render::MeshRenderer` and `render::Camera`; `render::Renderer` draws every ECS entity with both, camera-relative; swapchains gain depth and `render_meshes`; the sandbox window shows falling crates. |

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
| Physics spike 1: vendored Jolt v5.6.0 with the two *Numerical Recipes*-derived functions replaced; built with cross-platform determinism; determinism hash identical on MSVC, GCC and Clang | [PR #21](https://github.com/Avrenthrad/lykowareGE/pull/21), merge b5a19a6 | PR run [41762141044](https://github.com/Avrenthrad/lykowareGE/actions/runs/41762141044); `main` run [41763671709](https://github.com/Avrenthrad/lykowareGE/actions/runs/41763671709) passed all five jobs |
| Physics spike 2: `lykoware::physics::World`, generation-checked handles, basic shapes, per-tick stepping, ray casts, defined misuse behaviour | [PR #23](https://github.com/Avrenthrad/lykowareGE/pull/23), merge 346ee0d | PR run [41765080887](https://github.com/Avrenthrad/lykowareGE/actions/runs/41765080887) |
| Physics spike 3: height-field terrain; walking characters that jump, step up ledges and are held back by steep slopes; determinism hash identical on MSVC, GCC and Clang | [PR #24](https://github.com/Avrenthrad/lykowareGE/pull/24), merge 6a955c6 | PR run [41770820818](https://github.com/Avrenthrad/lykowareGE/actions/runs/41770820818) passed all five jobs; `main` run [41776019225](https://github.com/Avrenthrad/lykowareGE/actions/runs/41776019225) passed all five jobs |
| Physics spike 4: body-count benchmark (`perf.physics_bodies_256`, `perf.physics_bodies_1024`); step memory overflows to the heap instead of aborting | [PR #25](https://github.com/Avrenthrad/lykowareGE/pull/25), merge b5c75ed | PR run [41771495786](https://github.com/Avrenthrad/lykowareGE/actions/runs/41771495786); `main` run [41776131190](https://github.com/Avrenthrad/lykowareGE/actions/runs/41776131190) passed all five jobs |
| Product targets (ADR 0021, owner's answers) and owner acceptance of ADR 0020 (docs) | [PR #26](https://github.com/Avrenthrad/lykowareGE/pull/26), merge 8132130 | PR run [41785710544](https://github.com/Avrenthrad/lykowareGE/actions/runs/41785710544) passed all five jobs |
| Double-precision spike 1: Jolt built with double-precision positions; new determinism hashes identical on MSVC, GCC and Clang | [PR #27](https://github.com/Avrenthrad/lykowareGE/pull/27), merge 28b2592 | PR run [41786685034](https://github.com/Avrenthrad/lykowareGE/actions/runs/41786685034) passed all five jobs |
| Double-precision spike 2: `double` positions in the physics API; kinematic bodies never sleep; `physics.far_from_origin` and `perf.physics_bodies_1024_far` | [PR #28](https://github.com/Avrenthrad/lykowareGE/pull/28), merge f50b9dd | PR run [41812549670](https://github.com/Avrenthrad/lykowareGE/actions/runs/41812549670) passed all five jobs |
| ECS 1: Lykoware's own entity-component system (generation-checked entities, packed component storage, deterministic iteration); `ecs.world`, `perf.ecs_iterate_100k` | [PR #29](https://github.com/Avrenthrad/lykowareGE/pull/29), merge f80b909 | PR run [41815974698](https://github.com/Avrenthrad/lykowareGE/actions/runs/41815974698) passed all five jobs |
| ECS 2: removal hooks, `ChangeBuffer`, physics bodies and characters on entities (`physics_ecs`); `physics.ecs_link` despawn hash identical on MSVC, GCC and Clang | [PR #31](https://github.com/Avrenthrad/lykowareGE/pull/31), merge 5b46f08 | PR run [41818502123](https://github.com/Avrenthrad/lykowareGE/actions/runs/41818502123) passed all five jobs |

CTest on `main` (35 tests): `trace.api`, `clock.fixed_step`, `ecs.world`, `physics.jolt_math`, `physics.jolt_determinism`, `physics.world`, `physics.character`, `physics.far_from_origin`, `physics.ecs_link`, `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `input.mapping`, `window.lifecycle`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `perf.physics_bodies_256`, `perf.physics_bodies_1024`, `perf.physics_bodies_1024_far`, `perf.ecs_iterate_100k`, `foundation.smoke`, `cli.commands`, `cli.integration`, `rhi.instance`, `rhi.instance.no_driver`, `rhi.device`, `rhi.render_target`, `rhi.swapchain`, `sandbox.vulkan_window`, `shaders.spirv_valid`, `shaders.spirv_valid_fragment`, `rhi.pipeline`, `sandbox.missing_shaders`. The `rhi.*` and `sandbox.vulkan_window` tests run on lavapipe in Linux CI and report Skipped on the Windows runners, which have no Vulkan.

Per-PR detail (local checks, failures found and fixed, mutation checks) is in each pull request and in ADRs 0009–0018.

## Current development task

### 3D rendering, PR 1: the maths module

Owner: Claude. Branch `claude/math`, based on `main` at 5b46f08. In review.

- The owner chose on 2026-11-04 to write Lykoware's own maths rather than use GLM, and a first 3D milestone of lit falling crates drawn from the ECS. [ADR 0024](adr/0024-math.md) records the maths conventions (Proposed).
- `lykoware/math.hpp`: `Vec2`, `Vec3`, `Vec4`, `DVec3`, `Quat`, `Mat4` and their operations.
  - Right-handed, +Y up; column-major matrices.
  - Vulkan perspective with reversed, infinite depth.
  - `relative_to` for camera-relative positions.
  - `physics::Vec3`, `DVec3` and `Quat` are now aliases of these, with every physics hash unchanged.
- **New test `math.types`.** Ten deliberate breaks, each caught; two first slipped through and led to stronger tests.
- **Local checks:** 36/36 pass in Debug, ASan/UBSan and clang-tidy; every physics hash unchanged.
- **CI:** pending.

### 3D rendering, PR 2: meshes in the RHI

Owner: Claude. Branch `claude/rhi-mesh`, stacked on PR 1 (`claude/math`). In review.

- `rhi::Buffer` for vertex and index data.
- Pipelines with a position-and-normal vertex layout, a reversed-depth test, back-face culling and push constants.
- Render targets with a depth buffer, and `draw_meshes` for a list of indexed meshes. Every misuse is refused before anything reaches the GPU. [ADR 0025](adr/0025-rhi-meshes.md) (Proposed).
- **New test `rhi.mesh`** (lavapipe): a lit cube with lighting per face, depth order regardless of draw order, the winding convention, repeatable bytes, identical bytes 16 km out, and every misuse. Six deliberate breaks, each caught.
- **Local checks:** 37/37 pass in Debug, ASan/UBSan and clang-tidy, with no Vulkan validation errors. Under ASan, `rhi.mesh` joined the GPU tests that keep Vulkan drivers loaded, which avoids lavapipe's known false leak report; a deliberate leak in our own code is still caught.
- **CI:** pending.

### 3D rendering, PR 3: falling crates drawn from the ECS

Owner: Claude. Branch `claude/render-scene`, stacked on PR 2 (`claude/rhi-mesh`). In review.

- **`lykoware::Transform`:** the position and rotation physics writes and the renderer reads.
- **`render::Renderer`:** draws every entity with a `Transform` and a `MeshRenderer` (a lit, coloured, scaled cube for now), camera-relative, into an offscreen target or a window.
- **Window path:** swapchains gain a depth buffer and mesh drawing.
- **The sandbox's `--window --vulkan`** now shows a 5 × 5 grid of coloured crates tumbling onto a grey ground, replaced every 8 seconds. [ADR 0026](adr/0026-renderer.md) (Proposed).
- **New test `render.scene`:**
  - physics drops three crates in front of a wall;
  - colours, depth, ground and sky are checked at the right pixels;
  - two runs give identical bytes;
  - the scene 16 km out gives exactly the same image.

  It writes the frame under the build directory to look at.
- **Deliberate breaks:** four caught. Culling off is not caught here, as expected; `rhi.mesh` covers it. A weakness found and fixed: depth off first went unnoticed until the wall was added.
- **Local checks:** 38/38 pass in Debug, ASan/UBSan and clang-tidy, with no Vulkan validation errors.
- **CI:** pending.

## Next

Owner order, set 2026-11-02. It replaces the earlier "clocks first" order.

1. Vulkan RHI bring-up: done (#11–#18). Instance, device, offscreen render target read back into the image harness, swapchain, build-time Slang shaders, graphics pipelines, and the triangle offscreen and in the sandbox window.
2. Phase 1 clocks and fixed-step update loop: done (#19, ADR 0017).
3. Physics: Jolt accepted (ADR 0003) and vendored (#21, ADR 0018). The `lykoware::physics` API is merged (#23). Terrain and characters (#24) and the body-count benchmark (#25) are merged; the spike is done. The owner accepted ADR 0020 on 2026-11-04.
4. Hardware profiles, multiplayer intent, world scope and budgets: decided on 2026-11-04 (ADR 0021). Desktop PC only, RTX 4070 recommended at 1440p60, 16 GB RAM, single-player built co-op-ready, a 16 km+ world, double-precision positions.
5. Physics positions in double precision: done (#27, #28, ADR 0022).
6. The rest of Phase 1: the ECS is done (#29, #31, ADR 0023). 3D rendering is in progress (maths, then GPU buffers and depth, then falling crates drawn from the ECS); then scene loading, screenshots, command bus and replay, audio.

## Known issues and limitations

- No scene loader, audio, animation, world streaming or inference yet. With 3D rendering PRs 1–3: the renderer draws lit, coloured cubes from ECS entities with depth, camera-relative, offscreen and in the sandbox window. There are no meshes from files, textures, materials, shadows, more than one light, interpolation between ticks or window read-back, so window pixels are not checked automatically (offscreen renders are).
- GPU tests skip (exit 77, CTest "Skipped") where no Vulkan loader or driver is installed. Hosted Windows runners may skip them; a skip is not a pass.
- `lyko run` launches the console sandbox. `lyko screenshot` and `lyko run --scene` print `unsupported: no renderer` and exit 3. `lyko trace --frames N` is a CPU trace of the sandbox loop, not a GPU capture.
- The image harness compares a software test pattern with committed PPM fixtures, and Vulkan clears read back from a render target. Neither is a scene screenshot or the image/performance gate; `lyko screenshot` still exits 3.
- Provisional CPU budgets apply only to the hosted windows-2022 and ubuntu-24.04 runners. They are not an RTX 4070 gate.
- Window: a real Win32/X11/Wayland desktop window, focus changes and a real Vulkan surface are untested in CI. A Linux desktop window needs X11 or Wayland headers at configure time. A newly adopted gamepad starts with released buttons and centred axes until it reports a change.
- Vendored runtime sources: SDL 3.4.18 (HIDAPI inside it under the BSD-style licence, not GPL), Vulkan-Headers C headers (MIT option), volk (MIT) and Vulkan Memory Allocator 3.4.0 (MIT). Vendored build-time tools, not shipped: Slang (Apache-2.0 WITH LLVM-exception, one owner-accepted Unlicense file), SPIRV-Tools and SPIRV-Headers. See [third_party/README.md](../third_party/README.md).
- The first configure on a machine builds the shader tools, which takes about 15 extra minutes once per tool version. CI keeps them in the free Actions cache.
- Sanitizers are Linux-only. ThreadSanitizer is deferred until there are concurrent engine systems.
- The CLI is a developer process wrapper, not the future command bus. See [CLI.md](CLI.md).
- Pax review, patent review (including US 10,926,179 for Living Memory) and legal clearance remain required before anything is public. None has been done.
- GitHub artifact storage is exhausted. Uploads stay opt-in; builds and tests do not depend on them. No storage was bought and no paid operation was run.

## Handoff

- State: `main` at 5b46f08 contains PRs #2, #4–#21, #23–#29 and #31. The Vulkan RHI bring-up, Phase 1 clocks, the physics spike, double-precision positions and the ECS (with physics on entities) are complete; Jolt is vendored behind the `lykoware::physics` API, with cross-platform determinism verified on MSVC, GCC and Clang.
- Open decisions for the owner: accepting or rejecting ADRs 0008–0012, 0014–0017 and 0022–0024 (merging did not accept them); the working assumptions in ADR 0021 (2–4 player co-op, 6-core CPU, 8 GB VRAM at minimum settings, camera-relative rendering); Cursor's #22 (ADR 0019).
- In review: `claude/math` (#32, 3D rendering PR 1, ADR 0024), `claude/rhi-mesh` (#33, PR 2, ADR 0025) and `claude/render-scene` (PR 3, ADR 0026), stacked in that order. Also open: #30 (physics handle slots retire instead of wrapping, from a separate session) and Cursor's #22. Next concrete action: the next Phase 1 items: scene loading (text-authored scenes through the CLI), real screenshots (`lyko screenshot`, a golden-image gate), command bus and replay, audio.
- Owner check wanted: run `lykoware_sandbox --window --vulkan` on the RTX 4070 desktop. With 3D rendering PR 3, a 5 × 5 grid of coloured crates should tumble onto a grey ground under a blue sky and drop again every 8 seconds.
- Checklist: no box ticked by this update. Phase 1 and the image/performance gate stay open.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and the [ADRs](adr/README.md). Check latest `main` and open PRs before starting. Update this file with exact commits, checks, failures and next actions.
