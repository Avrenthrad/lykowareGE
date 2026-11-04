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
| [0020](0020-physics-api.md) | Proposed | `lykoware::physics::World`: generation-checked body handles, basic shapes, per-tick stepping, ray casts, defined misuse behaviour, no Jolt types in public headers |

Use [the template](TEMPLATE.md). Record context, options, decision/status, contracts, consequences and verification. Use a new ADR to supersede an accepted choice; do not silently rewrite its history.

[Pre-brief records](../decisions/README.md) remain historical and are not part of this numbering sequence.
