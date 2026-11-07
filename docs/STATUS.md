# Lykoware status

## Active: hardware evidence assessment (Codex)

Branch `codex/hardware-evidence`, base main `992e791`; ADR 0060. Scope: Windows GPU inventory, fresh measurement directories, cross-run integrity and optional explicit offscreen budget assessment. No runtime API changes or calibrated performance claims. Completion: failure-path tests, native CTest and hosted Windows/Linux checks; target-PC measurements remain open.

## Current consolidation — accepted integration, 8 November 2026

PR [#65](https://github.com/Avrenthrad/lykowareGE/pull/65) merged as `aacff25`, retaining all runtime/character and notices/validation heads #37–64. Validated source `13c097d` passed all five hosted jobs (Windows Debug/Release and three Linux profiles). Independent clean-checkout validation passed 70/70 in each Linux profile, zero skips, with install/staged execution. [Consolidation evidence and limitations](CONSOLIDATION_2026-11-08.md) records exact hashes, CI, fixes and the Windows GPU skips. Stacked PRs are closed as incorporated, with contributor branches retained. Older blocked-CI/unmerged/next-task notes below are historical.

**Open acceptance:** NVIDIA RTX 4070 is owner-reported; actual VRAM/driver, target-PC desktop behavior, frame pacing, RAM/VRAM and calibrated budgets remain unverified. Phase 1 remains open. Existing local Windows validation and offscreen measurement commands are in the evidence document. Proposed ADRs remain Proposed until separately accepted.

**Next engineering work:** reusable validated IK chains/scratch and allocation measurements, followed by locomotion, foot orientation and body constraints. Existing terrain alignment is stationary vertical correction, not full walking foot locking. Target-PC evidence can proceed in parallel.

## Historical implementation and verification records

The entries below describe their named commits and dates. References to blocked CI, unmerged branches and next tasks are historical, superseded by the current consolidation above and GitHub's PR state.


## Ten-step stationary terrain alignment batch — draft #64

Codex, `codex/stationary-foot-placement`, based on #63. [Batch checklist](FOOT_ALIGNMENT_BATCH.md) records all ten locally implemented/verified steps; ADR 0059 records the contract. Opt-in imported humanoids retain mapped independent direct legs, convert actor/fitting/world targets correctly, query physics without writes, bound/smooth/reset vertical contacts, apply poses bilaterally and report applied ECS render palettes. Profiles accept `foot_ik` and rig-specific `foot_clearance`.

Clean unchanged source `2cc97b5dc055323a14db12a952f940f524c1a8aa` passed 68/68 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-foot-placement-validation/report.json`; fingerprint `278304d59bfa728ca68305b7534f1f179bf15731bda778dfb6dc6ec908e9706c`. Real Jolt contacts, fitted/yawed 64 km bridge, bounded corrections, 30/60 Hz smoothing, lost/teleport/nonfinite/long-delta resets, transactional second-leg failure, custom clearance and unchanged physics hash pass. Generated glTF CLI requires actual contacts, no runtime failures and changed render palettes; it also passes with synchronization validation enabled. Five external mutants were caught, including a missing render-palette update that still reported contacts. No faulty/generated artifacts committed. Later handoff edits are documentation only.

Stationary vertical foot-joint alignment only: no walking locks, foot orientation/normals, slope classification, platform anchors, pelvis balancing or broader helper/scale support. Reference validation/temporary allocations remain; prepare chains/scratch and measure before crowd use. Full foot IK, Windows/hardware, hosted CI and owner merge stay open. Main/Claude branches unchanged.


## Two-bone pose application — draft #63

Codex, `codex/pose-ik`, stacked on #62. ADR 0058 adds `animation::pose_two_bone`: validates skeleton/pose, direct chain, unit scales and unchanged rest lengths; applies global shortest-arc root/knee corrections through parent rotations; verifies FK before committing pose and geometry outputs. Supports pose/output aliasing, preserves local translations/scales, ancestor/sibling/end local rotations and writes no physics. Targets and hints are skeleton model coordinates, not actor/world coordinates.

Clean unchanged source `fbf7468a384179aa21aa175b183474cd4214171b` passed 67/67 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-pose-ik-validation/report.json`. Independent FK targets under rotated/translated ancestors, varied 3D targets, aliases, exact/near-antiparallel corrections and unsupported/stretched/collapsed chains pass. A near-180-degree case exposed rounding to an exact half-turn; double shortest-arc construction fixed it. Four external faults (ignored parent, skipped root/knee correction, early failure overwrite) were caught. No faulty artifacts committed.

Limits: direct links only, unit scales, no foot world-orientation/joint limits or terrain integration. Helper chains accepted for mapping/inspection remain unsupported by pose application. This correctness-first operation validates the whole pose/skeleton and uses temporary vectors/copies; prepared chains and reusable scratch are needed before per-frame crowd budget claims. No allocation-free claim. Windows/hardware/hosted CI gates and owner merge remain open. Subsequent handoff edits are documentation only.

Next: bridge mapped leg roles, rest/bend calibration and world→model targets, then terrain probes/foot planting and smoothing. Master checklist separates verified engine operation from unimplemented runtime integration.


## Two-bone IK geometry — draft #62

Codex, `codex/two-bone-ik`, stacked on #61. ADR 0057 adds engine `animation::solve_two_bone` in `lykoware/ik.hpp`: double-space root/knee/end positions, no-stretch inner/outer reach clamping, explicit direction/bend-hint fallbacks and transactional errors for invalid or precision-collapsed geometry. No pose, joint rotation or physics mutation; no terrain-foot-placement claim.

Clean unchanged source `9fec63321e6bd067bbd7f9d8e9873b8de80d2de0` passed 67/67 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-two-bone-ik-validation/report.json`. Analytical 3D bends, both limb orderings/reach limits, explicit/collinear fallbacks, far origin, 200 varied targets, subnormal normalisation, length boundaries and failure atomicity pass. Four external deliberate faults were caught: removed clamp, ignored hint, ignored lower length and overwritten failure output. No faulty/generated artifacts committed.

Next: validate mapped rest-pose limb lengths/bend planes, convert solved positions to joint rotations, then add terrain probes and foot planting. Master checklist 02.11 split into these steps; only Linux verification checked. Windows/hardware/hosted CI and owner merge remain open. Subsequent handoff edits are documentation only.


## Humanoid leg-role mapping — draft #61

Codex, `codex/rig-roles`, stacked on #60. ADR 0056 adds optional profile `bones`: seven exact unique names for pelvis and left/right hip, knee and foot. Mapping resolves loaded skeleton indices, requires distinct roles and correct ancestor chains, allows intermediate helper joints, and prints resolved names/indices. Invalid mappings are refused before character upload; no pose or physics mutation. Mapping does not enable IK or retargeting or certify usable limb lengths.

Verified clean unchanged source `1699fc8ef43ebfbcc5dafb8df7fb564b59ef0ab4`: 66/66 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-rig-roles-validation/report.json`. Generated seven-joint mapped playback and detached-foot refusal, lookup uniqueness/alias/ancestry/transactional tests pass. Existing unmapped one-joint playback remains supported. Imported-player check also passes Debug with synchronization validation enabled. Later handoff edits are documentation only. Windows/hardware/hosted CI gates remain open; Joshua merges.

Next: rest-length/bend-plane validation and a tested two-bone IK solver before terrain foot placement. Orientation/capsule alignment and retargeting remain open. No main or Claude branch changes.


## Authored character profiles — draft #60

Codex, `codex/character-profiles`, stacked on #59. ADR 0055 adds versioned JSON model/idle/walk/optional-height setup through `--character-profile FILE`. Relative model paths resolve from the profile folder. Unknown/duplicate keys, unsupported versions, invalid types, excessive nesting, files over 64 KiB and mixed individual flags are refused. No bone-role mapping, retargeting or physics changes are claimed.

Clean unchanged source `c469c81eb43c0640f50f3fd487fe96005936bb48` passed 66/66 each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-character-profiles-validation/report.json`. Profile playback/refusals use an original generated one-joint glTF; the same CLI test also passes with synchronization validation enabled. Subsequent handoff changes are documentation only. Windows, target hardware and hosted CI gates stay open; Joshua merges.

Master checklist now records completed Linux verification substeps for #59/#60 separately from pending feature/merge gates. Next: rig bone-role mapping and inspection, broader locomotion states, then foot IK. Main/Claude branches are unchanged.


## Character fitting continuation — draft #59

Codex, `codex/character-fitting`, based on #58. ADR 0054 adds `--character-height METRES` and bind-height/joint-count diagnostics. A reusable sample helper uniformly scales and centres imported bind geometry, putting bind feet at the interpolated controller origin. Skeleton/clip data and physics/camera are unchanged. Authored placement remains default. This is partial visual fitting, not retargeting or automatic capsule fitting.

Verified clean unchanged source `1aab2131ee4c745f552dd75500e18d3b114608b9`: 66/66 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-character-fitting-validation/report.json`. Analytical off-centre feet/head placement, transactional failure, invalid numeric flags and real CLI playback are checked. Imported-player test also passes with Khronos synchronization validation enabled. Windows, hardware and hosted CI gates remain open. No merge; Joshua retains merge ownership.

Checklist 02.02 is split into explicit substeps; visual fitting marked implemented/in review, remaining profiles, bone mapping, orientation/capsule alignment and retargeting open. Next: authored character profiles and locomotion configuration before foot IK. Later handoff edits are documentation only.


## Imported playable character continuation (Codex, draft #58)

Branch `codex/imported-player`, stacked on #57. Source `aabb116afe40edc0a20652d190274606e963a42a` passed 66/66 each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass, clean unchanged source. Report `/workspace/lykoware-imported-player-validation/report.json`. Additional Debug `sandbox.imported_player` passes with Khronos synchronization validation enabled. Windows/hardware and required hosted CI remain unverified/blocked; Joshua merges.

Sandbox accepts `--play --character FILE [--idle-clip NAME] [--walk-clip NAME]`. Uses glTF skin/clip/material import and GPU poses; one-joint/one-clip generated character and refusal cases are checked. Clip clocks use authored duration. No procedural visor is attached to imported rigs. Models retain authored metres, feet origin and -Z forward; physics capsule is unchanged. No rig fitting, retargeting or root motion is claimed. ADR 0053 records defaults and limits. Next: rig fitting and locomotion setup before foot IK. No main merge or roadmap exit.


Records what is implemented and verified on `main`. Plans live in [ROADMAP.md](ROADMAP.md); task ownership in [WORK_BOARD.md](WORK_BOARD.md). Older per-branch handoffs are in the merged pull requests and git history.

## Current continuation: GPU skinning and animated player (Codex, not merged)

Draft [#57](https://github.com/Avrenthrad/lykowareGE/pull/57), `codex/gpu-skinning`, stacked on #56. [ADR 0052](adr/0052-gpu-skinning.md) adds an optional skinned shader/layout, `Renderer::add_skinned_mesh`, per-instance `render::SkinPose`, and reusable dynamic-uniform arenas owned by offscreen targets or fenced swapchain frame slots. Geometry uploads once; poses copy matrices. Static geometry has a shader fast path; static-only renderers retain their original format/shader.

The sandbox's `--play` now draws an original procedural seven-joint mannequin with sampled idle/walk clips, smooth speed-based blending, interpolated placement and first-person hiding. Its visor follows the animated head. Animation does not write the physics controller. No external model, editor or paid service is needed.

- **Frozen source:** `1ef0bbbe45cf9dacf83e593aa009a6f3948f7da5`, clean and unchanged, passed **65/65 CTests each** Linux Debug, ASan/UBSan and clang-tidy 18, no skips. Configure/build/install/installed CLI passed. GCC 14.2, CMake/CTest 3.31.6, Mesa lavapipe LLVM 19. Fingerprint `623c653e9e0ae35038c6948ba4c027a76e3c4cbe4a2683bd6f6e1c6a5469f0a3`. Report: `/workspace/lykoware-health-gpu-skinning-final/report.json`. Subsequent handoff edits are documentation only.
- **GPU checks:** actual CPU/GPU image comparisons cover blended translation, out-of-plane rotation/non-uniform scale and lit normals, four active influences including joint 255, independent instances, missing-pose bind geometry, singular-normal fallback, repeated updates, arena growth/cap refusal/recovery and renderer recreation. Malformed zero-valued owned mesh/texture IDs are now rejected without slot underflow. Static fast-path captures match the original pipeline; existing scene golden passes.
- **Test-strength evidence:** an initial flat-normal case missed a shader using the position matrix for normals. The out-of-plane case was added and demonstrably rejects that valid-but-wrong shader. The final suite also rejects a shader dropping the fourth influence. Fault shaders/captures are external diagnostic artifacts, never committed.
- **Validation layers:** the 65-test matrix ran without a validation layer. A local development copy of Khronos validation 1.4.309 was then enabled, with its log confirming `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`. `render.gpu_skinning`, `lifecycle.restart_vulkan` and the 60-frame `sandbox.play` all passed in Debug, no skips or reported validation errors. Supplemental output: `/workspace/gpu-sync-validation-tests.log`. This is software Vulkan coverage, not physical GPU proof.
- **Captures:** ignored `build/<preset>/gpu-skinning/mannequin.ppm` and pose diagnostics. They show actual GPU-rendered test content, not finished character art.
- **Costs/limits:** palettes copy 16 KiB per mesh part, bounded at 4096 nonempty draw palettes; arenas retain high-water capacity. Animation-enabled static meshes use a larger stride. These costs need profiling before crowds/world budgets. Windows/MSVC and physical-GPU performance remain unverified; [CI run 42161276822](https://github.com/Avrenthrad/lykowareGE/actions/runs/42161276822) failed five jobs with zero steps, with GitHub citing failed payments/spending limit. No billing or gate change was made.

Next: imported character/clip playback, locomotion setup and foot IK, followed by animation/physics blending and ragdoll. Joshua retains merge ownership; main, Phase 1 acceptance and hardware gates are unchanged.

## CPU skinning reference: PR #56 (not merged)

The owner asked Codex to check Claude's latest pushes and continue. Exact Claude glTF skins source `15383a27bcc788191bb6646f5d01f27a4673ea47` passed **61/61 CTests in each** Linux Debug, ASan/UBSan and clang-tidy 18, no skips, with clean unchanged repository inputs. Configure/build/install/installed CLI passed, using GCC 14.2, CMake/CTest 3.31.6 and Mesa lavapipe LLVM 19. The original audio/renderer ownership probes now both exit 0 and reject foreign/stale IDs. These are independent local results; current hosted CI still fails before steps execute, and Windows remains unverified.

Implemented in draft [#56](https://github.com/Avrenthrad/lykowareGE/pull/56), `codex/skinned-rendering`, stacked on Claude's [#55](https://github.com/Avrenthrad/lykowareGE/pull/55): [ADR 0051](adr/0051-cpu-skinning-reference.md), `animation::deform_mesh` and real rendered pose captures. Four-influence affine blends, inverse-transpose normals, validated inputs and transactional/alias-safe output; topology and UVs preserved. This is a CPU correctness/capture reference, with no per-frame upload path or production GPU-skinning claim.

- **New source verified:** `69612eb853fe6764555951b267edcb65cd19f174`, clean and unchanged, passed **63/63 each** Linux Debug, ASan/UBSan and clang-tidy 18, no skips. Configure/build/install/installed CLI passed. Fingerprint `506a2bbba9ef8d77610d346bbe383d228e3fe73c79fe7e4f6fcd9e6062fb87a1`; raw local report `/workspace/lykoware-health-skinning-reference/report.json`. Subsequent handoff edits are documentation only.
- **Checks:** `animation.deform` uses analytical values and failure/aliasing cases; `render.skinning` loads a generated glTF clip, samples and deforms it, captures actual Vulkan pixels, compares with independently calculated geometry, checks UV texture and independent poses. Existing static-scene golden still passes. Six deliberate CPU faults were all detected.
- **Captures:** ignored `build/<preset>/render-skinning/skin-rest.ppm`, `skin-posed.ppm` and `skin-independent.ppm`.
- **Next:** measured GPU palette/vertex path and animated sandbox character. Windows/MSVC, physical GPU performance and hosted CI remain unverified/blocked. Joshua retains merge ownership; no main merge or roadmap exit is claimed.

## Current health evaluation and local validation: PR #47 (not merged)

[Health report](HEALTH_CHECK_2026-11-06.md): exact merged main `7616ce3` passed 41/41 Linux Debug tests. Combined snapshot `48b0da9` on `codex/health-full-stack` passed 55/55 in each of Linux Debug, ASan/UBSan and clang-tidy 18, with no skips; both source trees stayed clean and unchanged. Installation and a real software-Vulkan scene capture passed. Additional probes reproduced two P2 ownership defects in pending audio and renderer APIs; see report and saved repros before merging those modules.

Codex's draft #47, stacked on #45, adds [local validation](LOCAL_VALIDATION.md) and ADR 0046. Eleven runner unit tests include failure-path subcases. No pending engine module is merged by this work. Fresh Windows execution, target-GPU budgets and hosted CI remain blocked/unverified as detailed in the health report. Phase 1 remains open.

## Runtime package notices: PR #45 (not merged)

Owner: Codex, branch `codex/runtime-package-notices`, based on `main` `7616ce3`. [ADR 0045](adr/0045-runtime-package-notices.md) adds installed runtime attributions and a readable index, including SDL subcomponent notices. Copied files preserve bytes; generated embedded excerpts normalize line endings. The `RuntimeNotices` install component supplements existing full JSON/Wuffs install rules without changing their ownership or adding dependencies.

- **Baseline local checks:** Linux Debug 42/42 CTests pass (Mesa lavapipe/LLVM 19); final component adjustment retested with `package.runtime_notices`.
- **Combined local check:** source `6984cbd` plus Claude's GPU-timing head `2c41f62` (including #37–#44), local integration commit `15c0b33`: Linux Debug **52/52 passed, no skips**. Includes full/component installs, byte checks, CRLF excerpt handling, relocation, missing-anchor failures and Wuffs notices. CMake merged without a code conflict; the work-board prose was reconciled to retain both owners. The integration branch is local only; Claude's branches are unchanged.
- **Negative check:** temporarily misdirecting Jolt's installed notice makes the package test fail; restored rules pass. Missing excerpt anchors also fail rather than producing partial notices.
- **CI blocked, Windows unverified:** [run 42036213824](https://github.com/Avrenthrad/lykowareGE/actions/runs/42036213824) failed all five jobs with no steps executed. GitHub's annotation states: "The job was not started because recent account payments have failed or your spending limit needs to be increased." This is not a compiler/test result. No billing setting, paid operation or CI gate was changed.

Next: review #45 alongside Claude's stack, restore CI through the owner, and verify Windows Debug/Release before merge. Existing target-PC, Phase 1 exit and public-release gates remain open. This is packaging evidence, not legal clearance.

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
| [0042](adr/0042-script-driven-scenes.md) | Proposed (in review) | Lua scripts drive scenes through the scene's own commands: `scene::ScriptDriver` gives scripts `spawn`, `destroy`, `set_velocity` and `position` and calls `on_tick(tick)`; every change is a recorded command, so a script run replays without the script; `lyko run --scene ... --script FILE [--record FILE]`; `Scene::position`. |
| [0043](adr/0043-renderer-golden-capture.md) | Proposed (in review) | A real renderer capture through `lyko screenshot` is compared with a committed golden (`tests/golden/falling-crates-160x120.ppm`, lavapipe) at tolerance 2 with at most 16 differing pixels; hardware GPUs report Skipped until they have goldens. |
| [0044](adr/0044-follow-camera.md) | Proposed (in review) | Phase 2 begins: `FollowCamera` orbits a pivot at the player's yaw and pitch, probes the way with five rays fanned from the pivot, keeps 0.2 m clear of walls, pulls in at once and eases out; `camera.follow` checks 504 views in a cluttered room never see through or from inside a wall. |
| [0047](adr/0047-playable-character.md) | Proposed (in review) | A playable character: mouse motion and relative mouse mode; `read_player_intent` (keyboard, mouse, gamepad) and `PlayerCharacter` (camera-relative movement, a 0.1 s jump buffer, zoom to first person); a shared obstacle course in the sandbox's `--play` and in `player.course`, where a fixed-seed bot checks checkpoints, ground penetration and the camera every frame. |
| [0048](adr/0048-drawing-between-steps.md) | Proposed (in review) | The player, follow camera and sandbox crates are drawn between the last two physics steps by the clock's alpha, so they move evenly at any frame rate (0.0278 m every 144 Hz frame at 4 m/s, instead of 0 or 0.067 m), one step behind. |
| [0049](adr/0049-skeletal-animation-core.md) | Proposed (in review; owner: "Continue" to the recommendation) | Skeletal animation core, our own: skeletons, clips in glTF's model (step, linear with slerp, cubic spline), sampling with looping, pose blending and skinning matrices; checked against hand-worked values. |
| [0050](adr/0050-gltf-skins-and-animations.md) | Proposed (in review) | Our glTF reader reads skinned models: the mesh in bind space with each vertex's joints and weights, a skeleton of the skin's joints and every node above them (an exporter's armature keeps its transform), and each animation as a clip. `read`/`read_model` are unchanged. |

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

### Phase 2, PR 5: reading glTF skins and animations

Owner: Claude. Branch `claude/gltf-skins`, stacked on the animation core. In review as [#55](https://github.com/Avrenthrad/lykowareGE/pull/55), opened by Codex during the continuation handoff; source unchanged.

- [ADR 0050](adr/0050-gltf-skins-and-animations.md) (Proposed).
- **`gltf::read_skinned`/`load_skinned`** read a skinned model:
  - the mesh in bind space, with each vertex's joints and weights;
  - a skeleton of the skin's joints and every node above them, so an exporter's armature node keeps its transform;
  - each animation as a clip on that skeleton.
- **Unchanged:** `read` and `read_model`.
- **New test `assets.gltf_skin`:** a skinned arm built in memory. Its animation swings the forearm vertex to exactly where hand-worked skinning puts it. Also covered: node matrices, flat-shaded vertices and every refusal.
- **Deliberate breaks:** 31 of 31 caught. Two first exposed weak checks (a negative 16-bit rotation part, STEP mid-interval), which were strengthened.
- **Independent continuation check (Codex):** exact `15383a2` passes 61/61 each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; source clean and unchanged. Includes build, installation and installed CLI. Windows remains unverified.
- **CI:** blocked for the whole account (see audio PR 1).
- **Next:** draw skinned meshes, then an animated character in the sandbox.

### Phase 2, PR 4: skeletal animation core

Owner: Claude. Branch `claude/animation-core`, stacked on drawing between steps. In review as [#54](https://github.com/Avrenthrad/lykowareGE/pull/54).

- [ADR 0049](adr/0049-skeletal-animation-core.md) (Proposed). The owner chose to start skeletal animation ("Continue", 2026-11-06).
- **`lykoware::animation`** (our own code):
  - skeletons and clips in glTF's model (step, linear with slerp, cubic spline);
  - sampling with looping or clamping;
  - pose blending for transitions;
  - skinning matrices that move a mesh's vertices with its joints.
- **New test `animation.pose`:** checked against hand-worked values: a skinned shoulder–elbow–hand arm, every interpolation, looping, blending and every refusal.
- **Deliberate breaks:** 21 of 21 caught.
- **Local checks:** 60/60 pass in Debug, ASan/UBSan and clang-tidy, with no skips; the GCC strict-warning sweep finds nothing in the new code.
- **CI:** blocked for the whole account (see audio PR 1).
- **Next:** read glTF skins and animations into these types, then draw skinned meshes, then an animated character in the sandbox.

### Phase 2, PR 3: drawing between physics steps

Owner: Claude. Branch `claude/tick-smoothing`, stacked on the playable character. In review as [#53](https://github.com/Avrenthrad/lykowareGE/pull/53).

- [ADR 0048](adr/0048-drawing-between-steps.md) (Proposed).
- **The problem:** physics steps at 60 Hz. On a 144 Hz monitor the player and camera stood still on some frames and jumped a whole step on others, so the view juddered.
- **The fix:** the player (`drawn_feet(alpha)`, `view(seconds, alpha)`), the camera and the sandbox's crates are drawn between the last two steps by the clock's alpha.
  - Presentation only: the simulation and its hashes are unchanged.
  - The cost is one step (16.7 ms) of display delay.
- **Test:** `player.course` checks the blend. At 144 Hz, walking at 4 m/s, the drawn feet and camera move 0.0278 m every frame; the latest step alone moves 0 or 0.067 m. The course bot now draws with alpha too.
- **Deliberate breaks:** 6 of 6 caught.
- **Local checks:** 59/59 pass in Debug, ASan/UBSan and clang-tidy, with no skips; the GCC strict-warning sweep finds nothing in the new code.
- **CI:** blocked for the whole account (see audio PR 1).

### Phase 2, PR 2: playable character

Owner: Claude. Branch `claude/playable-character`, stacked on the follow camera. In review as [#52](https://github.com/Avrenthrad/lykowareGE/pull/52).

- [ADR 0047](adr/0047-playable-character.md) (Proposed). ADR numbers 0045 and 0046 are Codex's (#45, #47).
- **Input:**
  - `InputState` gains the mouse's motion each frame;
  - `Window` can hold the mouse (relative mode) for mouse-look.
- **`PlayerCharacter`** (`lykoware/player.hpp`) is steered by keyboard, mouse or gamepad:
  - movement follows where the camera faces; walk 4 m/s, run 7 m/s, jump 5 m/s;
  - a jump waits up to 0.1 s for the ground;
  - the wheel zooms in to first person.
- **Sandbox `--play`** (with `--vulkan`): walk the obstacle course beside the falling crates.
  - Click holds the mouse; Esc lets it go; R starts again.
  - It runs headless on lavapipe.
- **New test `player.course`:** a fixed-seed bot plays the same course through the same input path, at frame rates from 30 to 144 Hz.
  - **Course:** terrain bumps, a 20° ramp, a 1.5 m drop, stairs, a ledge that needs a jump, a 1.8 m drop, a low tunnel and a 60° slope.
  - **Ground:** the feet never stay sunk (1.3 mm deepest).
  - **Camera:** never through or inside a wall. The tunnel ceiling pulls it in 95 of 98 frames.
  - **Repeatability:** the same seed repeats bit for bit.
- **Deliberate breaks:** 31 of 31 caught (movement, jumps, dead zones, mouse and stick directions, zoom, pivot, ownership, input motion, the window's relative mode and the camera's obstruction handling).
- **Local checks:** 59/59 pass in Debug, ASan/UBSan and clang-tidy, with no skips. New `sandbox.play` runs `--play` headless. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion` sweep finds nothing in the new code.
- **CI:** blocked for the whole account (see audio PR 1).
- **Owner's call:** how it feels in play (`lykoware_sandbox --window --vulkan --play`).

### Phase 2, PR 1: follow camera

Owner: Claude. Branch `claude/follow-camera`, stacked on the capture golden. In review as [#51](https://github.com/Avrenthrad/lykowareGE/pull/51).

- [ADR 0044](adr/0044-follow-camera.md) (Proposed).
- **Phase 2 begins** (owner: "Continue", 2026-11-05) with the camera-obstruction deliverable.
- **`FollowCamera`:** a third-person orbit camera that never looks through walls; distance 0 is first person.
- **New test `camera.follow`:** 504 views around a cluttered room. No wall is ever between the pivot and the eye, and the near plane never enters a wall.
- **Deliberate breaks:** 9 of 9 caught. One first exposed a missing test case, which was added.
- **Local checks:** 57/57 pass in Debug, ASan/UBSan and clang-tidy.
- **Next:** PR 2, a playable character in the sandbox with this camera, and fixed-seed obstacle-course fixtures (now in review, above).

### Real capture golden

Owner: Claude. Branch `claude/capture-golden`, stacked on scripting PR 2. In review as [#50](https://github.com/Avrenthrad/lykowareGE/pull/50).

- [ADR 0043](adr/0043-renderer-golden-capture.md) (Proposed).
- **The gap:** Phase 1's exit asks that "a real image capture passes its golden tolerance". Captures were only checked for repeating.
- **The test:** new `scene.capture_golden` renders the falling-crates scene through `lyko screenshot` and compares it with a committed 160×120 golden (57 KB), at tolerance 2 with at most 16 differing pixels. A failure writes a diff image.
- **Devices:** software Vulkan only for now. Hardware GPUs report Skipped.
- **Deliberate breaks:** 4 of 4 caught (sky colour, a skipped command, ambient light, field of view). A first, looser allowance missed a whole crate and was tightened.
- **Local checks:** 56/56 pass in Debug, ASan/UBSan and clang-tidy.
- **CI:** blocked for the whole account (see audio PR 1). CI's older lavapipe may need a reviewed golden or tolerance adjustment on its first run.

### Scripting, PR 2: scripts drive scenes

Owner: Claude. Branch `claude/script-commands`, stacked on scripting PR 1. In review as [#49](https://github.com/Avrenthrad/lykowareGE/pull/49).

- [ADR 0042](adr/0042-script-driven-scenes.md) (Proposed).
- **`scene::ScriptDriver`:** a script's `on_tick(tick)` runs each tick. Its `spawn{...}`, `destroy(id)` and `set_velocity(id, {x, y, z})` calls become ordinary scene commands, dispatched and recorded; `position(id)` reads the scene.
- **Recordings:** a script run can be written out with `--record` and replayed with `--commands` to the same state hash, without the script.
- **Entry point:** `lyko run --scene FILE --script FILE`. `samples/scenes/falling-crates.lua` matches `falling-crates.commands.json` bit for bit.
- **Tests:**
  - new `scene.script`;
  - `cli.integration` and `cli.commands` cover the new options.
- **Deliberate breaks:** 15 of 15 caught.
- **Local checks:** 55/55 pass in Debug, ASan/UBSan and clang-tidy.
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
- **Deliberate breaks:** 25 of 25 caught. Four of them first exposed weak tests, which were fixed.
- **Fuzzing:** 200,000 scripts under ASan/UBSan, with no crash or hang.
  - An earlier run hung inside `string.rep('', 2^40)`.
  - The sandbox now bounds that, and the matching `table.insert`, `remove` and `move` loops, which the instruction limit cannot stop.
- **Local checks:** 54/54 pass in Debug, ASan/UBSan and clang-tidy.
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

   - scripting: sandboxed Lua (ADR 0041) and scripts driving scenes from `lyko run --script` (ADR 0042).

   - a real capture checked against a committed golden (ADR 0043, Phase 1 exit item).

   Still open: measured hardware budgets.

## Known issues and limitations

- The merged renderer draws lit cubes with depth and camera-relative positions; #36 adds versioned box scenes, commands and interpolation snapshots. PBR materials (normal, metal-roughness, emissive maps), transparency, shadows, multiple lights, animation, streaming and inference remain future work. Window read-back/frame-pacing validation is still absent; offscreen pixels are checked.
- GPU tests skip (exit 77, CTest "Skipped") where no Vulkan loader or driver is installed. Hosted Windows runners may skip them; a skip is not a pass.
- `lyko run` launches the console sandbox; #36 added scene replay and capture (ADR 0031); in review, `lyko run --scene ... --script` drives a scene from Lua (ADR 0042). `lyko trace --frames N` is a CPU trace of the sandbox loop; in review, `lyko trace --gpu --frames N` traces the Vulkan window with GPU frame times (ADR 0039). GPU spans are placed at the CPU's submit time, not with calibrated timestamps, and a swapchain frame's GPU time can include waiting for its image.
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
- In review: `claude/audio-mixer` (Audio PR 1, ADR 0032, [#37](https://github.com/Avrenthrad/lykowareGE/pull/37)) and, stacked on it, `claude/audio-output` (Audio PR 2, ADR 0033, [#38](https://github.com/Avrenthrad/lykowareGE/pull/38)) and `claude/audio-spatial` (Audio PR 3, ADR 0034, [#39](https://github.com/Avrenthrad/lykowareGE/pull/39)), then `claude/gltf-meshes` (model loading, ADR 0035, [#40](https://github.com/Avrenthrad/lykowareGE/pull/40)), then `claude/image-decode` (textures PR 1, ADR 0036, [#41](https://github.com/Avrenthrad/lykowareGE/pull/41)), then `claude/gpu-textures` (textures PR 2, ADR 0037, [#42](https://github.com/Avrenthrad/lykowareGE/pull/42)), then `claude/gltf-materials` (textures PR 3, ADR 0038, [#43](https://github.com/Avrenthrad/lykowareGE/pull/43)), then `claude/gpu-timings` (GPU timings in traces, ADR 0039, [#44](https://github.com/Avrenthrad/lykowareGE/pull/44)), then `claude/window-lifecycle` (focus, minimise, close and restart, ADR 0040, [#46](https://github.com/Avrenthrad/lykowareGE/pull/46)), then `claude/script-lua` (scripting PR 1, sandboxed Lua, ADR 0041, [#48](https://github.com/Avrenthrad/lykowareGE/pull/48)), then `claude/script-commands` (scripting PR 2, script-driven scenes, ADR 0042, [#49](https://github.com/Avrenthrad/lykowareGE/pull/49)), then `claude/capture-golden` (real capture golden, ADR 0043, [#50](https://github.com/Avrenthrad/lykowareGE/pull/50)), then `claude/follow-camera` (Phase 2 PR 1, follow camera, ADR 0044, [#51](https://github.com/Avrenthrad/lykowareGE/pull/51)), then `claude/playable-character` (Phase 2 PR 2, playable character and obstacle course, ADR 0047, [#52](https://github.com/Avrenthrad/lykowareGE/pull/52)), then `claude/tick-smoothing` (Phase 2 PR 3, drawing between physics steps, ADR 0048, [#53](https://github.com/Avrenthrad/lykowareGE/pull/53)), then `claude/animation-core` (Phase 2 PR 4, skeletal animation core, ADR 0049, [#54](https://github.com/Avrenthrad/lykowareGE/pull/54)), then `claude/gltf-skins` (Phase 2 PR 5, glTF skins and animations, ADR 0050). CI cannot run at the moment: since 2026-11-05 every job fails before a runner starts. GitHub's annotation on each job reads "The job was not started because recent account payments have failed or your spending limit needs to be increased" (Billing & plans). These PRs are checked locally only. #30's fix reached `main` through #36 (80a0640), so the owner can close #30. Cursor's #22 is open. Codex's [#45](https://github.com/Avrenthrad/lykowareGE/pull/45) (installed licence notices, ADR 0045) is open against `main`.
- Handle ownership fixes (2026-11-05), from Codex's health check in [#47](https://github.com/Avrenthrad/lykowareGE/pull/47). Both defects were reproduced and fixed in the PR that introduced them, then merged forward through the stack:
  - **`VoiceId`** (#37) carries its mixer's owner token. A mixer can no longer be copied or moved, so one mixer cannot stop another's voice.
  - **`MeshId`** (#40) and **`TextureId`** (#42) carry their renderer's token. An id kept from a destroyed renderer is no longer drawn by its replacement. Mesh parts (#43) refuse another renderer's texture id.
  - Each fix has a test that fails without it (checked by removing the check). ADRs 0032, 0035, 0037 and 0038 record the rule (ADR 0030).
- Open decisions for the owner:
  - accepting or rejecting ADRs 0008–0012, 0014–0017, 0022–0026, 0030–0044 and 0047–0050 (merging did not accept them);
  - how to restore CI: fix the failed payment or spending limit under Billing & plans, or a self-hosted runner on the owner's PC. Any spending needs the owner's approval;
  - whether #44 and #46 complete their Phase 1 exit items (nothing is ticked);
  - the working assumptions in ADR 0021 (2–4 player co-op, 6-core CPU, 8 GB VRAM at minimum settings, camera-relative rendering);
  - Cursor's #22 (ADR 0019).
- Next concrete action: once CI runs again, get the stacked PRs green (Windows/MSVC first). With both scripting PRs and the capture golden (ADR 0043), every Phase 1 deliverable and every exit item that needs no hardware is in review. What remains for Phase 1 is the owner's acceptance and the measured hardware budgets on the owner's PC. Phase 2 (character feel on terrain) has begun alongside the reviews: the follow camera, a playable character on an obstacle course, smooth drawing between steps the skeletal animation core and glTF skins and animations are in review; next come skinned drawing and an animated character, then foot IK and ragdoll. Codex's #45 covers the install licence notices.
- Owner checks wanted: run `lykoware_sandbox --window --vulkan` on the RTX 4070 desktop (a 5 × 5 grid of coloured crates tumbling onto a grey ground and dropping again every 8 seconds, each knocking as it lands, through the default sound device), `tools/validation/measure-scene.ps1` (ADR 0031), `lykoware_sandbox --window --vulkan --model FILE` with a textured .glb exported from Blender (ADR 0035, ADR 0038), `lyko trace --gpu --frames 300` opened in Perfetto (ADR 0039), and in the sandbox window: minimise it (CPU should stay low) and hold W while alt-tabbing away and back (nothing should stay pressed) (ADR 0040).
- Checklist: no box ticked by this update. Phase 1 and the image/performance gate stay open.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and the [ADRs](adr/README.md). Check latest `main` and open PRs before starting. Update this file with exact commits, checks, failures and next actions.
