# Lykoware status

Records what is implemented and verified on `main`. Plans live in [ROADMAP.md](ROADMAP.md); task ownership in [WORK_BOARD.md](WORK_BOARD.md). Older per-branch handoffs are in the merged pull requests and git history.

## Review follow-up verification

- Runtime PR [#35](https://github.com/Avrenthrad/lykowareGE/pull/35), `codex/core-contract-hardening`: [ADR 0030](adr/0030-runtime-contract-hardening.md), world-specific handles, transactional attachment/storage, exclusive cleanup hooks, world-local hook order and allocation-free moves. All five CI jobs passed on source `f113b45` in [run 41826863572](https://github.com/Avrenthrad/lykowareGE/actions/runs/41826863572), including Windows Debug/Release and 39 CTests; the follow-up commit changes handoff prose only.
- Integration PR [#36](https://github.com/Avrenthrad/lykowareGE/pull/36), `codex/scene-command-replay`: [ADR 0031](adr/0031-scenes-and-command-replay.md), versioned scene/command schemas with stable IDs, read-only interpolated snapshots, CLI replay/capture/report and a Windows measurement runner. Preserves Claude's #30 and merged #32–#34. Local Linux Debug: **41/41 passed, no skips**, with Mesa lavapipe (LLVM 19), including 180-tick hashes at 30/60/144 Hz and irregular frames, real repeated captures and far-origin/read-only GPU checks. Staged CLI replay and real capture from `/tmp` pass, including shader deployment and the JSON MIT notice. All five CI jobs passed on source `26f7332` ([run 41827916577](https://github.com/Avrenthrad/lykowareGE/actions/runs/41827916577)): Windows Debug/Release, Linux Debug, ASan/UBSan and clang-tidy 18. Hosted Windows GPU tests report Skipped; Linux uses software Vulkan. The subsequent handoff/build-concurrency change caps first-build compiler jobs at two and validates job counts; current PR-head checks govern merge readiness.
- [REVIEW_FOLLOWUP](REVIEW_FOLLOWUP.md) records completion gates. Actual Windows target-PC performance remains unverified. Joshua merged #35 and #36 on 2026-11-04 (`main` 7616ce3, both `main` runs passed all five jobs); Phase 1 remains open.

## Decisions

| ADR | Status | Subject |
| --- | --- | --- |
| [0001](adr/0001-language.md) | Accepted | C++20 and CMake 3.25+ |
| [0002](adr/0002-graphics.md) | Accepted | Vulkan 1.3 from C++ behind a Lykoware-owned RHI. Vulkan-Headers, volk and VMA are vendored (ADRs 0009–0011). |
| [0003](adr/0003-physics.md) | **Accepted by owner 2026-11-04** | Jolt Physics v5.6.0 (MIT), natively from C++ behind a Lykoware API. The two *Numerical Recipes*-derived functions are replaced by our own; cross-platform determinism is on; the initial float-position choice was superseded by double positions in ADR 0022. Vendored and integrated. |
| [0004](adr/0004-data-formats.md) | Accepted | Canonical JSON for machine data, TOML for hand-edited config, glTF 2.0 meshes. The review branch vendors nlohmann/json 3.11.3 for the first scene/command caller. |
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
| [0030](adr/0030-runtime-contract-hardening.md) | Proposed (merged in #35) | World-specific runtime owner tokens on handles, transactional ECS storage and attachment, exclusive physics links. |
| [0031](adr/0031-scenes-and-command-replay.md) | Proposed (merged in #36) | Versioned JSON scenes with stable authored IDs, fixed-tick command replay, read-only presentation snapshots, CLI replay and capture; nlohmann/json 3.11.3 kept private (the owner chose to keep it on 2026-11-05). |
| [0032](adr/0032-audio-mixer.md) | Proposed (in review; owner chose "our own mixer") | `lykoware::audio`: our own 64-voice mixer to 48 kHz stereo float, bit-identical however the output is split into calls; constant-power panning, ramped gains, fixed-point linear resampling; our own WAV reader. No device yet. |
| [0033](adr/0033-audio-output.md) | Proposed (in review; owner chose SDL3 for playback) | `audio::Output`: SDL3 plays the mixer, rendered on SDL's audio thread under a lock; starts paused; dummy and disk drivers for tests; closing the last window no longer shuts down all of SDL. |
| [0034](adr/0034-spatial-audio.md) | Proposed (in review; owner chose 3D sound tied to ECS entities) | `audio::AudioSource` component and `SpatialAudio`: sounds play from entities' Transforms with inverse-distance falloff and stereo pan, computed relative to the listener in double precision; the sandbox's crates knock as they land. |
| [0035](adr/0035-gltf-meshes.md) | Proposed (in review; owner chose "write our own", shapes first) | `gltf::read`/`gltf::load`: our own glTF 2.0 reader into `MeshData` (one merged mesh per model, node transforms applied, strict bounds, external buffers confined to the model's folder); `Renderer::add_mesh` and `MeshRenderer::mesh`; normals use the inverse transpose; sandbox `--model FILE`. |
| [0036](adr/0036-image-decoding.md) | Proposed (in review; owner chose "vendor Wuffs") | `decode_image`: PNG and JPEG into 8-bit RGBA through vendored Wuffs v0.4.0-alpha.12 (MIT option; only PNG and JPEG compiled); checksums checked; 16,384 px and 2^26-pixel limits checked before allocation. |
| [0037](adr/0037-gpu-textures.md) | Proposed (in review) | `rhi::Texture`: RGBA8 sRGB or Unorm with CPU-made, gamma-correct mipmaps and wrap/filter settings; one descriptor set per texture with a device-wide layout; `VertexLayout::PositionNormalUv` and `GraphicsPipelineDesc::texture`; the lit shader multiplies the base colour by a texture (1x1 white by default); `Renderer::add_texture`, `MeshRenderer::texture`, `MeshData::uvs`. |
| [0038](adr/0038-gltf-materials.md) | Proposed (in review) | `gltf::read_model`/`load_model`: texture coordinates, base colour materials, textures, samplers and decoded images (confined to the model's folder); `render::MeshPart` and `add_mesh(mesh, parts)` draw a model's primitives with their own materials (`rhi::MeshDraw::first_index`); the sandbox's `--model` shows textured models. |
| [0039](adr/0039-gpu-timings.md) | Proposed (in review) | GPU timings from Vulkan timestamp queries: `Device::gpu_timestamps`, `RenderTarget::last_gpu_time_ns`, `Swapchain::take_gpu_frame_times`; `CpuTrace::add_gpu_span` puts them on a named GPU track; the sandbox's `--window --vulkan --frames N --trace FILE` and `lyko trace --gpu` write CPU and GPU traces. |
| [0040](adr/0040-window-lifecycle.md) | Proposed (in review) | `Window::has_focus()` and `minimized()`; losing focus releases held keys and mouse buttons; the swapchain skips frames while the window is minimised and the sandbox sleeps instead of spinning; tests for platform close and quit, and three full shutdown-and-start cycles of window, audio, physics, ECS and the Vulkan stack in one process. |
| [0041](adr/0041-lua-scripting.md) | Proposed (in review; owner chose the recommendation) | Scripting: vendored Lua 5.4.9 (MIT) compiled as C++ behind `lykoware::script::Script`: a sandbox without files, processes, clocks, module loading or bytecode; memory and instruction limits that pcall and coroutines cannot escape; a fixed hash seed and host-seeded `math.random`; host functions and calls with nil/boolean/integer/number/string values. |

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
| 3D rendering 1: Lykoware's own maths module (`math.types`) | [PR #32](https://github.com/Avrenthrad/lykowareGE/pull/32), merge e2473cc | PR run [41820018128](https://github.com/Avrenthrad/lykowareGE/actions/runs/41820018128); `main` run [41824907164](https://github.com/Avrenthrad/lykowareGE/actions/runs/41824907164) passed all five jobs |
| 3D rendering 2: meshes in the RHI: buffers, depth, culling, push constants (`rhi.mesh`) | [PR #33](https://github.com/Avrenthrad/lykowareGE/pull/33), merge 06ee9b3 | PR run [41820726840](https://github.com/Avrenthrad/lykowareGE/actions/runs/41820726840); `main` run [41824925378](https://github.com/Avrenthrad/lykowareGE/actions/runs/41824925378) passed all five jobs |
| 3D rendering 3: falling crates drawn from the ECS, camera-relative (`render.scene`); the sandbox window shows them | [PR #34](https://github.com/Avrenthrad/lykowareGE/pull/34), merge 855e01e | PR run [41823030117](https://github.com/Avrenthrad/lykowareGE/actions/runs/41823030117); `main` run [41824945028](https://github.com/Avrenthrad/lykowareGE/actions/runs/41824945028) passed all five jobs |
| Codex: runtime contract hardening (ADR 0030, `ecs.allocation_recovery`) | [PR #35](https://github.com/Avrenthrad/lykowareGE/pull/35), merge 9a476fc | PR run [41826863572](https://github.com/Avrenthrad/lykowareGE/actions/runs/41826863572); `main` run [41831650885](https://github.com/Avrenthrad/lykowareGE/actions/runs/41831650885) passed all five jobs |
| Codex: scenes, command replay and CLI capture (ADR 0031, `scene.replay`, `scene.cli_capture`); carries #30's physics slot retirement | [PR #36](https://github.com/Avrenthrad/lykowareGE/pull/36), merge 7616ce3 | PR run [41827916577](https://github.com/Avrenthrad/lykowareGE/actions/runs/41827916577); `main` run [41831704372](https://github.com/Avrenthrad/lykowareGE/actions/runs/41831704372) passed all five jobs |

CTest on `main` (41 tests): `trace.api`, `clock.fixed_step`, `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `input.mapping`, `window.lifecycle`, `rhi.instance`, `rhi.instance.no_driver`, `rhi.device`, `rhi.render_target`, `rhi.swapchain`, `shaders.spirv_valid`, `shaders.spirv_valid_fragment`, `rhi.pipeline`, `rhi.mesh`, `render.scene`, `sandbox.vulkan_window`, `sandbox.missing_shaders`, `physics.jolt_math`, `physics.jolt_determinism`, `physics.world`, `math.types`, `scene.cli_capture`, `scene.replay`, `ecs.world`, `ecs.allocation_recovery`, `physics.ecs_link`, `physics.character`, `physics.far_from_origin`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `perf.physics_bodies_256`, `perf.physics_bodies_1024`, `perf.ecs_iterate_100k`, `perf.physics_bodies_1024_far`, `foundation.smoke`, `cli.commands`, `cli.integration`. The GPU tests (`rhi.*`, `render.scene`, `sandbox.vulkan_window`, `scene.cli_capture`) run on lavapipe in Linux CI and report Skipped on the Windows runners, which have no Vulkan.

Per-PR detail (local checks, failures found and fixed, mutation checks) is in each pull request and in ADRs 0009–0018.

## Current development task

### Audio, PR 1: the mixer and WAV reader

Owner: Claude. Branch `claude/audio-mixer`, based on `main` at 7616ce3. In review.

- The owner chose audio as the next Phase 1 system on 2026-11-05: playback through SDL3, our own mixer, 3D positional sound on ECS entities, deterministic tests. This PR is the mixer; [ADR 0032](adr/0032-audio-mixer.md) (Proposed).
- **`lykoware/audio.hpp`:**
  - `Sound`: immutable mono or stereo float audio; `Sound::load_wav` is our own WAV reader (PCM 8/16/24/32-bit, 32-bit float, extensible).
  - `Mixer`: 64 voices with generation-checked `VoiceId`s; gain, pan and pitch; looping; gain, pan and master changes ramped over 5 ms; `render` into 48 kHz interleaved stereo float.
  - **Deterministic:** the same calls give bit-identical output however the output is split into calls, with integer playback positions and no fused multiply-adds.
- **New test `audio.mixer`** (no device; exact values). 14 deliberate breaks, each caught. Review found a real determinism bug: reusing the most recently freed slot made the summing order depend on how output was split. A new voice now takes the lowest free slot, and the test checks it.
- **Also:** `scene.cli_capture` (from #36) renders on lavapipe, so sanitizer builds now keep Vulkan drivers loaded for it, as for the other GPU tests (ADR 0015). Without that it fails under ASan on a machine whose lavapipe reports its known false leak; CI's lavapipe did not.
- **Local checks:** 42/42 pass in Debug, ASan/UBSan and clang-tidy. A GCC `-Wshadow -Wconversion` sweep of the new files is clean.
- **CI:** to be recorded.

### Audio, PR 2: output through SDL3

Owner: Claude. Branch `claude/audio-output`, stacked on PR 1 (`claude/audio-mixer`). In review.

- [ADR 0033](adr/0033-audio-output.md) (Proposed). SDL's audio subsystem is on: WASAPI on Windows; PipeWire, PulseAudio or ALSA on Linux when found; dummy and disk drivers for tests.
- **`lykoware/audio_output.hpp`:** `audio::Output` owns a `Mixer` that SDL's audio thread renders under a lock. Game code reaches the mixer through `lock()`. The device starts paused; `resume()` and `pause()`; `driver_name()` and `device_format()` for diagnostics.
- **Window fix:** closing the last window used to call `SDL_Quit`, which would also stop audio. Each session now stops only its own subsystems.
- **New test `audio.output`:**
  - the audio thread pulls audio and ends voices;
  - pause works;
  - game-thread changes are safe while the audio thread renders;
  - driver rules;
  - audio survives the last window closing;
  - a disk-driver capture equals the mixer's own output, bit for bit.
- **Deliberate breaks:** 6 of 7 caught; the seventh is a guard SDL duplicates. A one-off ThreadSanitizer build is clean, and catches a removed lock.
- **Local checks:** 43/43 pass in Debug, ASan/UBSan and clang-tidy. The GCC warning sweep is clean.
- **CI:** blocked for the whole account (see PR 1). Windows (WASAPI, MSVC) is unverified.
- **Next:** PR 3 (below).

### Audio, PR 3: 3D positional sound on ECS entities

Owner: Claude. Branch `claude/audio-spatial`, stacked on PR 2 (`claude/audio-output`). In review.

- [ADR 0034](adr/0034-spatial-audio.md) (Proposed).
- **`lykoware/audio_spatial.hpp`:**
  - `Listener`;
  - `Falloff`;
  - `place()`: gain falls with inverse distance; pan comes from the direction across the listener; computed relative to the listener in double precision;
  - the `AudioSource` component;
  - `SpatialAudio`: plays entities' sounds and keeps each voice where its entity is. Loops stop when their entity goes; one-shots finish where it was.
- **Sandbox:** each crate knocks as it lands, louder the harder it hits, from where it is relative to the camera. The knock is made in code (no audio files). `--no-audio` turns sound off; headless runs use the dummy driver.
- **New test `audio.spatial`** (exact values, including bit-identical placement 16 km out).
- **Deliberate breaks:** 11 caught. The 16 km check was first too weak (coordinates float holds exactly) and was strengthened.
- **Local checks:**
  - 44/44 pass in Debug, ASan/UBSan and clang-tidy;
  - the sandbox played 30 knocks under ASan/UBSan with no errors;
  - the GCC warning sweep is clean.
- **Recording:** an 8.9 s recording through the disk driver was shared with the owner.
- **CI:** blocked for the whole account (see PR 1).

### Model loading, PR 1: glTF meshes

Owner: Claude. Branch `claude/gltf-meshes`, stacked on audio PR 3 to avoid conflicts in shared files; it uses no audio code. In review.

- The owner chose on 2026-11-05 our own glTF reader (not cgltf), shapes first, textures later. [ADR 0035](adr/0035-gltf-meshes.md) (Proposed).
- **`lykoware/mesh.hpp`:** `MeshData`, `valid_mesh`, `bounds`.
- **`lykoware/gltf.hpp`:**
  - reads .gltf and .glb, with buffers in the .glb, in data URIs, or in separate files, and node transforms applied;
  - refuses malformed, out-of-bounds, extension-dependent and sparse data with the item named;
  - `load()` reads external buffers only from the model's folder.
- **Renderer:**
  - `add_mesh` returns a `MeshId`, and `MeshRenderer::mesh` draws it;
  - normals now use the inverse transpose, so uneven scale lights correctly.
- **Sandbox:** `--model FILE` shows a model beside the crates.
- **New tests:**
  - `assets.gltf`: models built in the test, nothing committed;
  - `render.model`: a loaded ramp drawn with the correct lighting, and the same image 16 km out;
  - `sandbox.model`.
- **Deliberate breaks:** 19 caught. The strip and fan checks were first too weak and were strengthened.
- **Fuzzing:** a local fuzz of 200,000 mutated files under ASan/UBSan, with no crash.
- **Local checks:** 47/47 pass in Debug, ASan/UBSan and clang-tidy. The warning sweep is clean.
- **CI:** blocked for the whole account (see audio PR 1).

### Textures, PR 1: PNG and JPEG decoding

Owner: Claude. Branch `claude/image-decode`, stacked on model loading. In review.

- The owner chose on 2026-11-05 to vendor Wuffs, and to do textures as three PRs: decoding; GPU textures; glTF materials. [ADR 0036](adr/0036-image-decoding.md) (Proposed).
- **Vendored:** `third_party/wuffs/` holds Wuffs v0.4.0-alpha.12, unmodified, under its MIT option. The 0.3 stable line has no JPEG decoder. Only the PNG and JPEG modules are compiled.
- **`lykoware/image_decode.hpp`:** `decode_image` returns 8-bit RGBA.
  - PNG checksums are checked (Wuffs skips them by default).
  - Size limits are checked before allocation.
  - Truncated files are refused.
- **New test `assets.image`:** the test encodes its own PNGs and JPEGs with exactly known pixels, so no image files are committed.
- **Deliberate breaks:** 7 of 8 caught; the eighth guard is also enforced by Wuffs.
- **Cross-check with PIL** (local): identical pixels for compressed PNGs and for baseline, progressive and 4:2:0 JPEGs.
- **Fuzzing:** a local fuzz of 200,000 mutated files under ASan/UBSan, with no crash.
- **Local checks:** 48/48 pass in Debug, ASan/UBSan and clang-tidy. The Wuffs notice is installed.
- **CI:** blocked for the whole account (see audio PR 1).

### Textures, PR 2: textures on the GPU

Owner: Claude. Branch `claude/gpu-textures`, stacked on textures PR 1. In review.

- [ADR 0037](adr/0037-gpu-textures.md) (Proposed).
- **`rhi::Texture`:**
  - sRGB or Unorm RGBA8;
  - mipmaps made on the CPU, averaged in linear light, so they are the same on every GPU;
  - wrap and filter settings;
  - uploaded through a staging buffer.
- **Pipelines:** they can read one texture; the descriptor layout is shared and owned by the Device. A new vertex layout carries texture coordinates.
- **Lit shader and renderer:**
  - the engine's lit shader multiplies the base colour by a texture;
  - the renderer gains `add_texture` and `MeshRenderer::texture`;
  - untextured meshes use a 1×1 white texture and look as before;
  - `rhi.mesh` keeps the old untextured shader as a test shader.
- **New tests:**
  - `rhi.texture`: exact texels, sRGB decoding, gamma-correct mipmaps (a far checkerboard gives 128; averaging sRGB values would give 55), the three wrap modes, and misuse;
  - `render.texture`: a red|blue textured quad lit to the formula's value.
- **Deliberate breaks:** 12 caught.
- **Local checks:** 50/50 pass in Debug, ASan/UBSan and clang-tidy.
- **CI:** blocked for the whole account (see audio PR 1).

### Textures, PR 3: glTF materials and texture coordinates

Owner: Claude. Branch `claude/gltf-materials`, stacked on textures PR 2. In review.

- [ADR 0038](adr/0038-gltf-materials.md) (Proposed).
- **`gltf::read_model` and `load_model`:** return the mesh with texture coordinates, one part per primitive, and the base colour materials, textures (with sampler settings) and decoded images they use, each once. Images can be in buffer views, data URIs or files beside the model.
- **Renderer:** draws a mesh as parts, each with its own colour and texture. The RHI gains `MeshDraw::first_index`.
- **Sandbox:** `--model` shows a model's own materials and textures.
- **Tests:**
  - `assets.gltf` gains materials cases (PNGs made by the test);
  - `render.model` draws a two-material model and checks each part's lit colour;
  - `sandbox.model` now loads a textured .glb.
- **Deliberate breaks:** 15 caught.
- **Fuzzing:** a local fuzz of 150,000 mutated textured models under ASan/UBSan, with no crash.
- **Local checks:** 50/50 pass in Debug, ASan/UBSan and clang-tidy.
- **CI:** blocked for the whole account (see audio PR 1).

### GPU timings in traces

Owner: Claude. Branch `claude/gpu-timings`, stacked on textures PR 3. In review as [#44](https://github.com/Avrenthrad/lykowareGE/pull/44).

- [ADR 0039](adr/0039-gpu-timings.md) (Proposed).
- **RHI:** Vulkan timestamp queries time each `RenderTarget` render (`last_gpu_time_ns()`) and each swapchain frame (`take_gpu_frame_times()`, with the CPU submit time). `Device::gpu_timestamps()` says whether the GPU supports them.
- **Trace:** `CpuTrace::add_gpu_span` puts GPU spans on a second track named "GPU". CPU-only traces are unchanged.
- **Sandbox and CLI:** `lykoware_sandbox --window --vulkan --frames N --trace FILE` and `lyko trace --gpu --frames N` write a trace with each frame's CPU zones and GPU time. Every `--vulkan` run logs the average GPU time per frame.
- **Tests:**
  - `trace.api`, `rhi.render_target`, `rhi.mesh` and `rhi.swapchain` gain GPU timing checks;
  - new `sandbox.gpu_trace`;
  - `foundation.smoke` and `cli.commands` check the new options.
- **Deliberate breaks:** 17 of 18 caught. The miss, the sandbox's last read after its loop, cannot be seen on lavapipe.
- **Local checks:** 51/51 pass in Debug, ASan/UBSan and clang-tidy, with no validation errors.
- **CI:** blocked for the whole account (see audio PR 1).

### Window lifecycle: focus, minimise, close and restart

Owner: Claude. Branch `claude/window-lifecycle`, stacked on GPU timings. In review as [#46](https://github.com/Avrenthrad/lykowareGE/pull/46).

- [ADR 0040](adr/0040-window-lifecycle.md) (Proposed).
- **Window:**
  - `has_focus()` and `minimized()` follow the platform's events;
  - losing focus releases held keys and mouse buttons, so nothing sticks down after switching windows.
- **Swapchain:** skips frames while the window is minimised and keeps the swapchain. The sandbox sleeps instead of spinning.
- **Tests:**
  - `window.lifecycle` gains focus, minimise and close-event cases;
  - `rhi.swapchain` checks minimised frames;
  - new `lifecycle.restart` (window, audio, physics, ECS; runs everywhere) and `lifecycle.restart_vulkan` (plus the whole Vulkan stack) shut everything down and start again three times in one process.
- **Deliberate breaks:** 16 of 16 caught.
- **Local checks:** 53/53 pass in Debug, ASan/UBSan and clang-tidy, with no validation errors.
- **CI:** blocked for the whole account (see audio PR 1).

### Scripting, PR 1: sandboxed Lua

Owner: Claude. Branch `claude/script-lua`, stacked on window lifecycle. In review.

- [ADR 0041](adr/0041-lua-scripting.md) (Proposed). The owner chose the recommended option, Lua.
- **Vendored:** Lua 5.4.9 (MIT, checksum-verified) in `third_party/lua/`.
  - It is compiled as C++, so Lua errors unwind host code properly.
  - Only the safe libraries are compiled.
  - Its notice is installed.
- **`lykoware/script.hpp`:** `Script` with `run`, `call`, `define` (host functions), print capture, and memory and instruction limits.
  - Scripts cannot catch a stop with `pcall`, `xpcall`, coroutines or `__close`.
  - No files, processes, clocks, module loading, `load` or bytecode; finalizers are refused.
  - Replays repeat: the string hash seed is fixed and `math.random` is seeded by the host.
- **Tests:** new `script.sandbox`.
- **Deliberate breaks:** 21 of 21 caught. Four of them first exposed weak tests, which were fixed.
- **Local checks:** see ADR 0041.
- **CI:** blocked for the whole account (see audio PR 1).

## Next

Owner order, set 2026-11-02. It replaces the earlier "clocks first" order.

1. Vulkan RHI bring-up: done (#11–#18). Instance, device, offscreen render target read back into the image harness, swapchain, build-time Slang shaders, graphics pipelines, and the triangle offscreen and in the sandbox window.
2. Phase 1 clocks and fixed-step update loop: done (#19, ADR 0017).
3. Physics: Jolt accepted (ADR 0003) and vendored (#21, ADR 0018). The `lykoware::physics` API is merged (#23). Terrain and characters (#24) and the body-count benchmark (#25) are merged; the spike is done. The owner accepted ADR 0020 on 2026-11-04.
4. Hardware profiles, multiplayer intent, world scope and budgets: decided on 2026-11-04 (ADR 0021). Desktop PC only, RTX 4070 recommended at 1440p60, 16 GB RAM, single-player built co-op-ready, a 16 km+ world, double-precision positions.
5. Physics positions in double precision: done (#27, #28, ADR 0022).
6. The rest of Phase 1: ECS and the first lit renderer are merged (#29/#31–#34). #35 hardens contracts; #36 adds the first scene/command/replay/capture path (both merged). In review, all stacked (2026-11-05):
   - audio (#37–#39);
   - glTF models and textures (#40–#43);
   - GPU timings in traces (#44, Phase 1 exit item);
   - window focus, minimise, close and restart tests (#46, Phase 1 exit item).

   - scripting PR 1, sandboxed Lua (ADR 0041), with PR 2 (scene commands and a `--script` entry point) next.

   Still open: measured hardware budgets.

## Known issues and limitations

- The merged renderer draws lit cubes with depth and camera-relative positions; #36 adds versioned box scenes, commands and interpolation snapshots. PBR materials (normal, metal-roughness, emissive maps), transparency, shadows, multiple lights, animation, streaming and inference remain future work. Window read-back/frame-pacing validation is still absent; offscreen pixels are checked.
- GPU tests skip (exit 77, CTest "Skipped") where no Vulkan loader or driver is installed. Hosted Windows runners may skip them; a skip is not a pass.
- `lyko run` launches the console sandbox; #36 added scene replay and capture (ADR 0031). `lyko trace --frames N` is a CPU trace of the sandbox loop; in review, `lyko trace --gpu --frames N` traces the Vulkan window with GPU frame times (ADR 0039). GPU spans are placed at the CPU's submit time, not with calibrated timestamps, and a swapchain frame's GPU time can include waiting for its image.
- The image harness supports software fixtures and real Vulkan readback. #36 implements scene screenshots and repeated pixel/hash checks; this does not close the calibrated hardware performance gate.
- Provisional CPU budgets apply only to the hosted windows-2022 and ubuntu-24.04 runners. They are not an RTX 4070 gate.
- Window: a real Win32/X11/Wayland desktop window, focus changes and a real Vulkan surface are untested in CI; the window tests push the platform's focus, minimise and close events instead (ADR 0040, in review). Wayland does not report minimise. A Linux desktop window needs X11 or Wayland headers at configure time. A newly adopted gamepad starts with released buttons and centred axes until it reports a change.
- Vendored runtime sources: SDL 3.4.18 (HIDAPI inside it under the BSD-style licence, not GPL), Vulkan-Headers C headers (MIT option), volk (MIT), Vulkan Memory Allocator 3.4.0 (MIT), Jolt 5.6.0 (MIT), nlohmann/json 3.11.3 (MIT, since #36) and, in review, Wuffs v0.4.0-alpha.12 (MIT option, ADR 0036) and Lua 5.4.9 (MIT, ADR 0041). Vendored build-time tools, not shipped: Slang (Apache-2.0 WITH LLVM-exception, one owner-accepted Unlicense file), SPIRV-Tools and SPIRV-Headers. See [third_party/README.md](../third_party/README.md).
- The first configure on a machine builds the shader tools, which takes about 15 extra minutes once per tool version. CI keeps them in the free Actions cache.
- Sanitizers are Linux-only. ThreadSanitizer is deferred until there are concurrent engine systems.
- The CLI keeps its build/test process wrapper and adds the shared native scene command handler in #36. Generic editor actions/undo and a scripting language are not implemented. See [CLI.md](CLI.md).
- Pax review, patent review (including US 10,926,179 for Living Memory) and legal clearance remain required before anything is public. None has been done.
- GitHub artifact storage is exhausted. Uploads stay opt-in; builds and tests do not depend on them. No storage was bought and no paid operation was run.

## Handoff

- State: `main` at 7616ce3 contains PRs #2, #4–#21, #23–#29 and #31–#36. The Vulkan RHI bring-up, Phase 1 clocks, the physics spike, double-precision positions, the ECS (with physics on entities), the first lit renderer (falling crates), contract hardening and versioned scenes with command replay and CLI capture are complete. Physics determinism is verified on MSVC, GCC and Clang.
- In review: `claude/audio-mixer` (Audio PR 1, ADR 0032, [#37](https://github.com/Avrenthrad/lykowareGE/pull/37)) and, stacked on it, `claude/audio-output` (Audio PR 2, ADR 0033, [#38](https://github.com/Avrenthrad/lykowareGE/pull/38)) and `claude/audio-spatial` (Audio PR 3, ADR 0034, [#39](https://github.com/Avrenthrad/lykowareGE/pull/39)), then `claude/gltf-meshes` (model loading, ADR 0035, [#40](https://github.com/Avrenthrad/lykowareGE/pull/40)), then `claude/image-decode` (textures PR 1, ADR 0036, [#41](https://github.com/Avrenthrad/lykowareGE/pull/41)), then `claude/gpu-textures` (textures PR 2, ADR 0037, [#42](https://github.com/Avrenthrad/lykowareGE/pull/42)), then `claude/gltf-materials` (textures PR 3, ADR 0038, [#43](https://github.com/Avrenthrad/lykowareGE/pull/43)), then `claude/gpu-timings` (GPU timings in traces, ADR 0039, [#44](https://github.com/Avrenthrad/lykowareGE/pull/44)), then `claude/window-lifecycle` (focus, minimise, close and restart, ADR 0040, [#46](https://github.com/Avrenthrad/lykowareGE/pull/46)), then `claude/script-lua` (scripting PR 1, sandboxed Lua, ADR 0041). CI cannot run at the moment: since 2026-11-05 every job fails before a runner starts. GitHub's annotation on each job reads "The job was not started because recent account payments have failed or your spending limit needs to be increased" (Billing & plans). These PRs are checked locally only. #30's fix reached `main` through #36 (80a0640), so the owner can close #30. Cursor's #22 is open. Codex's [#45](https://github.com/Avrenthrad/lykowareGE/pull/45) (installed licence notices, ADR 0045) is open against `main`.
- Open decisions for the owner:
  - accepting or rejecting ADRs 0008–0012, 0014–0017, 0022–0026 and 0030–0041 (merging did not accept them);
  - how to restore CI: fix the failed payment or spending limit under Billing & plans, or a self-hosted runner on the owner's PC. Any spending needs the owner's approval;
  - whether #44 and #46 complete their Phase 1 exit items (nothing is ticked);
  - the working assumptions in ADR 0021 (2–4 player co-op, 6-core CPU, 8 GB VRAM at minimum settings, camera-relative rendering);
  - Cursor's #22 (ADR 0019).
- Next concrete action: once CI runs again, get the stacked PRs green (Windows/MSVC first). Then scripting PR 2: scripts issue scene commands, and a `--script` entry point in the sandbox and `lyko`. Codex's #45 covers the install licence notices.
- Owner checks wanted: run `lykoware_sandbox --window --vulkan` on the RTX 4070 desktop (a 5 × 5 grid of coloured crates tumbling onto a grey ground and dropping again every 8 seconds, each knocking as it lands, through the default sound device), `tools/validation/measure-scene.ps1` (ADR 0031), `lykoware_sandbox --window --vulkan --model FILE` with a textured .glb exported from Blender (ADR 0035, ADR 0038), `lyko trace --gpu --frames 300` opened in Perfetto (ADR 0039), and in the sandbox window: minimise it (CPU should stay low) and hold W while alt-tabbing away and back (nothing should stay pressed) (ADR 0040).
- Checklist: no box ticked by this update. Phase 1 and the image/performance gate stay open.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and the [ADRs](adr/README.md). Check latest `main` and open PRs before starting. Update this file with exact commits, checks, failures and next actions.
