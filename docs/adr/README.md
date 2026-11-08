# Architecture decision records

Canonical directory: docs/adr. Decisions remain Proposed until resolved; distinguish owner requirements from implementation choices.

| ADR | Status | Subject |
| --- | --- | --- |
| [0000](0000-ownership-and-licensing.md) | Accepted policy | Full ownership, licensing, privacy and release gates |
| [0001](0001-language.md) | Accepted | C++20/CMake for runtime and native tooling |
| [0002](0002-graphics.md) | Accepted | Vulkan 1.3 from C++ behind an owned RHI |
| [0003](0003-physics.md) | Accepted by owner 2026-11-04 | Physics: Jolt v5.6.0 (MIT) behind a Lykoware API; *Numerical Recipes*-derived functions replaced locally; cross-platform determinism on; float precision for now |
| [0004](0004-data-formats.md) | Accepted | Canonical JSON, TOML config, glTF 2.0 meshes |
| [0005](0005-native-cli.md) | Accepted implementation | Minimal native build/test CLI |
| [0006](0006-cpu-trace.md) | Accepted implementation | CPU timer, scoped trace, provisional hosted budgets |
| [0007](0007-image-harness.md) | Accepted implementation | GPU-free image harness; not a renderer screenshot |
| [0008](0008-window-input.md) | Proposed | Vendored SDL3 behind a Lykoware window and input API |
| [0009](0009-vulkan-loading.md) | Proposed | Vulkan-Headers and volk; run-time loading; RHI instance; skip-not-pass GPU tests |
| [0010](0010-rhi-device.md) | Proposed | RHI device requirements, selection ranking, one graphics-and-compute queue |
| [0011](0011-offscreen-render-target.md) | Proposed | VMA 3.4.0; offscreen RGBA8 render target; dynamic-rendering clear; read-back into the image harness |
| [0012](0012-swapchain.md) | Proposed | Swapchain, two frames in flight, resize rebuilds; offscreen headless Vulkan window for CI; sandbox `--vulkan` |
| [0013](0013-shader-compiler.md) | Accepted by owner (Slang), conditional on spike | Shader compiler licence review; Slang without its glslang part, plus SPIRV-Tools; Unlicense file accepted |
| [0014](0014-shader-build.md) | Proposed | Host tools built once from vendored source into a cache; Slang → spirv-opt → spirv-val shader build; CI Actions cache |
| [0015](0015-graphics-pipeline.md) | Proposed | `rhi::Shader` and `rhi::GraphicsPipeline`, `RenderTarget::draw`, `shaderDrawParameters`, sanitizer builds keep drivers loaded |
| [0016](0016-runtime-shader-files.md) | Proposed | Programs load compiled shaders from a `shaders` folder beside the executable; build and install deploy them; sandbox draws the triangle |
| [0017](0017-clocks-fixed-step.md) | Proposed | Fixed-step simulation clock (60 Hz default, exact integer accumulator, interpolation alpha, per-frame limits); `FrameClock` |
| [0018](0018-jolt-integration.md) | Accepted by owner 2026-11-04 | Jolt built from vendored source via its own CMake with forced options; cross-platform determinism checked by a fixed state hash in every CI job; AVX2 CPU requirement |
| [0020](0020-physics-api.md) | Accepted by owner 2026-11-04 | `lykoware::physics::World`: generation-checked body and character handles, basic shapes, height fields, characters, per-tick stepping, ray casts, defined misuse behaviour, no Jolt types in public headers |
| [0021](0021-product-targets.md) | Accepted by owner 2026-11-04 | Desktop PC only (no handheld); RTX 4070 recommended at 1440p60, weaker minimum; 16 GB RAM; single-player built co-op-ready; one 16 km+ world; double-precision positions |
| [0022](0022-double-precision-positions.md) | Proposed (owner chose the direction in ADR 0021) | Jolt built with `DOUBLE_PRECISION` on; 64-bit positions in state hashes; public positions become `double` |
| [0023](0023-ecs.md) | Proposed (owner chose "write our own") | Lykoware's own ECS: generation-checked entities, packed per-type component storage, deterministic iteration |
| [0024](0024-math.md) | Proposed (owner chose "write our own") | Lykoware's own maths: vectors, quaternions, column-major matrices, reversed infinite-depth Vulkan projection, camera-relative positions |
| [0025](0025-rhi-meshes.md) | Proposed | RHI meshes: vertex and index buffers, depth, culling, push constants, `draw_meshes` |
| [0026](0026-renderer.md) | Proposed | First renderer: `Transform`, `MeshRenderer`, `Camera`, `Renderer` drawing ECS entities camera-relative; swapchain depth; falling crates in the sandbox |
| [0030](0030-runtime-contract-hardening.md) | Proposed | Runtime owner tokens, transactional ECS storage and exclusive physics links |
| [0031](0031-scenes-and-command-replay.md) | Proposed | Versioned scenes, stable authored IDs, fixed-tick commands and read-only presentation |
| [0032](0032-audio-mixer.md) | Proposed (owner chose "our own mixer") | Lykoware's own audio mixer: 64 voices, 48 kHz stereo float output, bit-identical however it is split into calls; constant-power panning, ramped gains, linear resampling; our own WAV reader |
| [0033](0033-audio-output.md) | Proposed (owner chose SDL3 for playback) | Audio output through SDL3: `audio::Output` owns a mixer that SDL's audio thread renders under a lock; dummy and disk drivers for tests; window and audio SDL sessions independent |
| [0034](0034-spatial-audio.md) | Proposed (owner chose 3D sound tied to ECS entities) | 3D positional sound: `AudioSource` component, `SpatialAudio` keeps voices where their entities are; inverse-distance falloff and stereo pan, camera-relative in double precision; crate knocks in the sandbox |
| [0035](0035-gltf-meshes.md) | Proposed (owner chose "write our own", shapes first) | Lykoware's own glTF 2.0 reader (.gltf/.glb, node transforms baked, strict bounds, folder-confined external buffers) into `MeshData`; `Renderer::add_mesh` and `MeshRenderer::mesh`; inverse-transpose normals; sandbox `--model FILE` |
| [0036](0036-image-decoding.md) | Proposed (owner chose "vendor Wuffs") | PNG and JPEG decoding through vendored Wuffs v0.4.0-alpha.12 (MIT option) into 8-bit RGBA; checksums checked; size limits before allocation |
| [0037](0037-gpu-textures.md) | Proposed | `rhi::Texture` (sRGB/Unorm, CPU-made gamma-correct mipmaps, wrap and filter), one descriptor set per texture, `PositionNormalUv` pipelines; the lit shader samples a base colour texture; `Renderer::add_texture` and `MeshRenderer::texture` |
| [0038](0038-gltf-materials.md) | Proposed | glTF `read_model`/`load_model`: TEXCOORD_0, base colour materials, textures and samplers, decoded images; models drawn as `MeshPart`s with their own colour and texture (`MeshDraw::first_index`); sandbox shows textured models |
| [0039](0039-gpu-timings.md) | Proposed | GPU timings from Vulkan timestamp queries: `RenderTarget::last_gpu_time_ns`, `Swapchain::take_gpu_frame_times`; `CpuTrace` GPU spans on a second track; sandbox `--window --vulkan --trace` and `lyko trace --gpu` |
| [0040](0040-window-lifecycle.md) | Proposed | `Window::has_focus`/`minimized`; focus loss releases held keys and mouse buttons; the swapchain skips frames while minimised; close and full-restart tests (`lifecycle.restart`, `lifecycle.restart_vulkan`) |
| [0041](0041-lua-scripting.md) | Proposed (owner chose the recommendation) | Scripting: vendored Lua 5.4.9 (MIT), compiled as C++, sandboxed behind `lykoware::script::Script` with memory and instruction limits, a fixed hash seed and host-seeded `math.random` |
| [0042](0042-script-driven-scenes.md) | Proposed | Lua scripts drive scenes through recorded commands: `scene::ScriptDriver` (`spawn`, `destroy`, `set_velocity`, `position`, `on_tick`), `Scene::position`, `lyko run --script FILE [--record FILE]`; a script's recording replays without it |
| [0043](0043-renderer-golden-capture.md) | Proposed | A real `lyko screenshot` capture of the falling-crates scene compared with a committed 160x120 golden (tolerance 2, at most 16 pixels); software Vulkan only; `scene.capture_golden` |
| [0044](0044-follow-camera.md) | Proposed | Phase 2: `FollowCamera`, a third-person orbit camera that probes the physics world with five rays from the pivot, pulls in at once and eases out, and never looks through walls; distance 0 is first person |
| [0047](0047-playable-character.md) | Proposed | Phase 2: `PlayerCharacter` and `read_player_intent` (keyboard, mouse-look in relative mode, gamepad), camera-relative movement, a 0.1 s jump buffer, zoom to first person; a shared obstacle course played by the sandbox's `--play` and a fixed-seed bot in `player.course` |
| [0048](0048-drawing-between-steps.md) | Proposed | Phase 2: the player, follow camera and sandbox crates are drawn between the last two physics steps by the clock's alpha (`PlayerCharacter::drawn_feet`, `view(seconds, alpha)`), so they move evenly at any frame rate, one step behind |
| [0049](0049-skeletal-animation-core.md) | Proposed | Phase 2: `lykoware::animation`, our own skeletal animation core: skeletons, glTF-model clips (step, linear with slerp, cubic spline), sampling, pose blending and skinning matrices |
| [0050](0050-gltf-skins-and-animations.md) | Proposed | Phase 2: `gltf::read_skinned`/`load_skinned`: a skinned mesh in bind space with per-vertex joints and weights, a skeleton of the skin's joints and every node above them, and animations as clips; `read`/`read_model` unchanged |
| [0051](0051-cpu-skinning-reference.md) | Proposed | CPU skinning reference: validated affine blends, inverse-transpose normals, transactional output and real glTF pose captures; correctness slice before GPU skinning |
| [0052](0052-gpu-skinning.md) | Proposed | GPU skinned meshes and per-instance poses; fenced dynamic-uniform arenas, static fast path and procedural animated playable mannequin |

| [0045](0045-runtime-package-notices.md) | Proposed | Preserve runtime dependency notices in installed packages, with artifact checks |

| [0046](0046-local-validation-evidence.md) | Proposed | Local preset validation with source provenance and explicit pass/skip/failure reports |

Use [the template](TEMPLATE.md). Record context, options, decision/status, contracts, consequences and verification. Use a new ADR to supersede an accepted choice; do not silently rewrite its history.

[Pre-brief records](../decisions/README.md) remain historical and are not part of this numbering sequence.

- [0053: Imported playable character clips](0053-imported-player.md) — Proposed, #58.
- [0054: Explicit visual character fitting](0054-character-fitting.md) — Proposed, #59.
- [0055: Authored sample character profiles](0055-character-profiles.md) — Proposed, #60.
- [0056: Explicit humanoid leg roles](0056-rig-roles.md) — Proposed, #61.
- [0057: Two-bone position IK reference](0057-two-bone-ik.md) — Proposed, #62.
- [0058: Apply two-bone position IK to poses](0058-pose-ik.md) — Proposed, #63.
- [0059: Opt-in stationary terrain foot alignment](0059-stationary-foot-placement.md) — Proposed, #64.
- [0060: Local hardware evidence and offscreen assessment](0060-hardware-evidence-assessment.md) — Proposed; metadata version 2, optional GPU inventory and explicit fixture-bound provisional budgets.

- [0061: Prepared two-bone IK and reusable scratch](0061-prepared-two-bone-ik.md) — Proposed; owned topology/rest snapshot, bounded scratch, transactional span output, retained foot-controller buffers.
