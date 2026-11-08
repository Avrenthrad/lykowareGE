# Shared work board

## Active batch: terrain turning and controller authority (Codex)

Branch `codex/terrain-turning-runtime`, based on #69 `8201a26` / merged `e050eec`; ADR 0064. Batch 02.06/02.07: bounded/interpolated presentation heading, retained idle-turn target, explicit controller-authority policy and profile settings, and real terrain-course/physics-preservation regressions. Root tracks remain visual-only; no extraction or physics teleporting is added. Native/hosted validation pending. Hardware/manual work stays deferred.

## Character import/alignment — merged #69

PR [#69](https://github.com/Avrenthrad/lykowareGE/pull/69) merged as `e050eec`. Exact clean source `8201a26` passed [all five hosted profiles](https://github.com/Avrenthrad/lykowareGE/actions/runs/42454397613) and 76/76 each local Linux profile, zero skips, with install/staged CLI. Report `/workspace/lykoware-character-import-final-validation/report.json`; fingerprint `357be572716b2ffc2bbb587f02c537b5146348c5bcbb0c63a2b1a961f3ab394b`. Shared profile/role/fitting/calibration APIs now back the sandbox and CPU-only `lyko inspect-character`. Visual yaw, explicit capsule/camera settings, UTF-8 paths, output preservation, rig subset diagnostics and actual yaw/terrain/render-palette checks pass. A Windows test literal was typed as float without weakening warnings. This completes batch 02.01/02.02c/02.02d; progress 10/15. Pose transfer is still item 02.10, not implied by compatibility diagnostics. Hardware/feel gates remain open.

## Locomotion and crossfades — merged #68, 8 November 2026

PR [#68](https://github.com/Avrenthrad/lykowareGE/pull/68) merged as `3c0fc99`. Exact clean source `e7224c8` passed [all five hosted profiles](https://github.com/Avrenthrad/lykowareGE/actions/runs/42431816271) and 74/74 each local Linux Debug/ASan/UBSan/clang-tidy 18, zero skips, with configure/build/install/staged CLI. Report `/workspace/lykoware-locomotion-final-validation/report.json`; unchanged fingerprint `219c85313c65fd470d03c6edb1e6751c4470d1b9f7add759eb1f96845afe5733`.

Generic owned pose crossfades capture the visible pose on interruption, validate before committing, support overlap and atomically copy all buffers under allocation failure. The sample has idle/walk/run/jump/fall/landing, speed/apex hysteresis, landing hold, original demonstration clips, respawn reset and a shared real PlayerCharacter/Jolt adapter. Profiles optionally map run/jump/fall/land clips; imports clear procedural clips and have explicit walk/idle fallbacks. State/fallback counters are logged. Windows Debug caught a temporary-string proxy allocation; retaining the error buffer fixed it without weakening the allocation test. 1,000 crossfade and full sample playback updates record 0 ordinary C++ allocations after setup. Reset/copy failure sites, six real Jolt states and imported selectors/refusals pass. Three external faults (missing capture, premature output, forgotten elapsed time) were caught; no faulty artifacts committed.

Basic presentation layer only: no root-motion driving, general animation graph, walking foot locks, production art or owner-reviewed character feel. ADR 0062 remains Proposed. Next work is the owner-requested [15-item batch](NEXT_15_BATCH.md); target-PC acceptance remains deferred while Joshua is mobile.

## Prepared IK runtime — merged #67, 8 November 2026

PR [#67](https://github.com/Avrenthrad/lykowareGE/pull/67) merged as `abc620b`. Exact clean source `6c5626c2478f8d523ff04fd84737858cbea8370b` passed [all five hosted jobs](https://github.com/Avrenthrad/lykowareGE/actions/runs/42427459608): Windows Debug/Release and Linux Debug/ASan/UBSan/clang-tidy 18. Independent local validation passed 72/72 each Linux profile, zero skips, with configure/build/install/staged CLI. Report `/workspace/lykoware-prepared-ik-validation/report.json`; unchanged fingerprint `e76e8e243c9da3a07e029f0bbf10c04b649de43b91785496da60d41d2c53dcdb`. Hosted Windows GPU skips remain separate from these CPU IK checks.

`PreparedTwoBone` owns topology/rest-length snapshots; `TwoBoneScratch` provides bounded reusable storage. Dynamic validation and transactional FK checks remain. The terrain controller prepares both legs once, retains candidate/FK buffers and the playable body retains/preallocates corrected output. Reference solver remains unchanged. 128 varied cases agree exactly; fallback/near-antiparallel/reach/alias/overlap/max-joint/lifetime/failure checks pass. Four external faults (ignored parent, missing output, premature output, removed length checks) were caught. Existing terrain/physics-hash/render-palette regressions pass. No faulty artifacts committed.

1,000 prepared five-joint solves recorded 0 ordinary C++ heap allocations versus 7,000 reference allocations. One GCC14 Debug VM microbenchmark took 15.8 vs 30.2 ms total. An external 256-joint/1,000-solve probe verified endpoint error 1.69e-7 m, 646 vs 1100 ms total. These are observations, not timing gates or target-PC/crowd guarantees. Scratch occupies roughly 30 KiB per instance; callers may share immutable plans with separate worker scratch. The sample currently retains scratch per controller. Failure strings/undersized caller outputs and other animation/render paths may allocate. ADR 0061 remains Proposed.

**Next:** locomotion states/transitions and foot orientation/body constraints, plus representative rig/crowd profiling. Prepared storage does not implement walking locks, helper/scaled chains or full foot IK. **Hardware work is parked while Joshua is mobile**; resume RTX 4070 inventory/measurements when he returns to the PC. No hardware or Phase 1 gate is closed.

## Hardware evidence tooling — merged #66, 8 November 2026

PR [#66](https://github.com/Avrenthrad/lykowareGE/pull/66) merged as `32261bf`; exact source `e803335` passed [all five hosted jobs](https://github.com/Avrenthrad/lykowareGE/actions/runs/42425150706). Local Debug passed 71/71; nine assessor tests with failure-path subcases pass. Windows Debug/Release also exercised the actual runner in PowerShell 7 and Windows PowerShell 5: synthetic >4 GiB inventory, failed optional driver queries, unavailable Vulkan and refusal to overwrite evidence. These synthetic/hosted results are not target-PC measurements; Windows Vulkan tests still skip without a device.

`tools/validation/measure-scene.ps1` now records version 2 source/executable/fixture provenance and optional NVIDIA VRAM/driver inventory, runs three fresh offscreen samples and writes `assessment.json`. Invalid data/overruns fail; missing or ambiguous inventory is incomplete. Optional provisional budgets bind to fixture hashes/configuration. No thresholds default silently. Invalid policy input replaces stale success evidence. Phase 1/recommended-profile acceptance stays false. [Usage and evidence scope](LOCAL_VALIDATION.md#rtx-4070-evidence-and-assessment); ADR 0060 remains Proposed.

**Next acceptance action:** run the measurement script on the owner’s Windows PC. RTX 4070 is still owner-reported; actual VRAM/driver, desktop behavior, presentation pacing, RAM/VRAM residency and calibrated budgets remain open. No connected Windows executor is available in this workspace. Prepared IK chains/scratch are now integrated in #67; broader locomotion and profiling can proceed while target-PC evidence is deferred.

## Current consolidation — accepted integration, 8 November 2026

PR [#65](https://github.com/Avrenthrad/lykowareGE/pull/65) merged as `aacff25`, retaining all runtime/character and notices/validation heads #37–64. Validated source `13c097d` passed all five hosted jobs (Windows Debug/Release and three Linux profiles). Independent clean-checkout validation passed 70/70 in each Linux profile, zero skips, with install/staged execution. [Consolidation evidence and limitations](CONSOLIDATION_2026-11-08.md) records exact hashes, CI, fixes and the Windows GPU skips. Stacked PRs are closed as incorporated, with contributor branches retained. Older blocked-CI/unmerged/next-task notes below are historical.

**Open acceptance:** NVIDIA RTX 4070 is owner-reported; actual VRAM/driver, target-PC desktop behavior, frame pacing, RAM/VRAM and calibrated budgets remain unverified. Phase 1 remains open. Existing local Windows validation and offscreen measurement commands are in the evidence document. Proposed ADRs remain Proposed until separately accepted.

**Next engineering work:** locomotion, foot orientation and body constraints; prepared chains/scratch and initial allocation measurements are integrated in #67. Existing terrain alignment is stationary vertical correction, not full walking foot locking. Target-PC evidence can proceed in parallel.

## Historical implementation and verification records

The entries below describe their named commits and dates. References to blocked CI, unmerged branches and next tasks are historical, superseded by the current consolidation above and GitHub's PR state.


## Ten-step stationary terrain alignment batch — draft #64

Codex, `codex/stationary-foot-placement`, based on #63. [Batch checklist](FOOT_ALIGNMENT_BATCH.md) records all ten locally implemented/verified steps; ADR 0059 records the contract. Opt-in imported humanoids retain mapped independent direct legs, convert actor/fitting/world targets correctly, query physics without writes, bound/smooth/reset vertical contacts, apply poses bilaterally and report applied ECS render palettes. Profiles accept `foot_ik` and rig-specific `foot_clearance`.

Clean unchanged source `2cc97b5dc055323a14db12a952f940f524c1a8aa` passed 68/68 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-foot-placement-validation/report.json`; fingerprint `278304d59bfa728ca68305b7534f1f179bf15731bda778dfb6dc6ec908e9706c`. Real Jolt contacts, fitted/yawed 64 km bridge, bounded corrections, 30/60 Hz smoothing, lost/teleport/nonfinite/long-delta resets, transactional second-leg failure, custom clearance and unchanged physics hash pass. Generated glTF CLI requires actual contacts, no runtime failures and changed render palettes; it also passes with synchronization validation enabled. Five external mutants were caught, including a missing render-palette update that still reported contacts. No faulty/generated artifacts committed. Later handoff edits are documentation only.

Stationary vertical foot-joint alignment only: no walking locks, foot orientation/normals, slope classification, platform anchors, pelvis balancing or broader helper/scale support. Reference validation/temporary allocations remain; prepare chains/scratch and measure before crowd use. Full foot IK, Windows/hardware, hosted CI and owner merge stay open. Main/Claude branches unchanged.


## Active ten-step batch: stationary terrain alignment (Codex)

Branch `codex/stationary-foot-placement`, based on #63 `d43b0f9`; ADR 0059. S01 calibration; S02 coordinate conversion; S03 opt-in profile; S04 retained roles; S05 physics probes; S06 bounded corrections; S07 smoothing/resets; S08 bilateral pose transaction; S09 diagnostics; S10 verification/checklist. Implemented and Linux-verified in #64; see the batch checklist above. This is stationary vertical alignment only, not dynamic foot locking, orientation or pelvis balancing. No physics writes or new performance claim; Windows/hardware/owner gates remain separate.


## Two-bone pose application — draft #63

Codex, `codex/pose-ik`, stacked on #62. ADR 0058 adds `animation::pose_two_bone`: validates skeleton/pose, direct chain, unit scales and unchanged rest lengths; applies global shortest-arc root/knee corrections through parent rotations; verifies FK before committing pose and geometry outputs. Supports pose/output aliasing, preserves local translations/scales, ancestor/sibling/end local rotations and writes no physics. Targets and hints are skeleton model coordinates, not actor/world coordinates.

Clean unchanged source `fbf7468a384179aa21aa175b183474cd4214171b` passed 67/67 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-pose-ik-validation/report.json`. Independent FK targets under rotated/translated ancestors, varied 3D targets, aliases, exact/near-antiparallel corrections and unsupported/stretched/collapsed chains pass. A near-180-degree case exposed rounding to an exact half-turn; double shortest-arc construction fixed it. Four external faults (ignored parent, skipped root/knee correction, early failure overwrite) were caught. No faulty artifacts committed.

Limits: direct links only, unit scales, no foot world-orientation/joint limits or terrain integration. Helper chains accepted for mapping/inspection remain unsupported by pose application. This correctness-first operation validates the whole pose/skeleton and uses temporary vectors/copies; prepared chains and reusable scratch are needed before per-frame crowd budget claims. No allocation-free claim. Windows/hardware/hosted CI gates and owner merge remain open. Subsequent handoff edits are documentation only.

Next: bridge mapped leg roles, rest/bend calibration and world→model targets, then terrain probes/foot planting and smoothing. Master checklist separates verified engine operation from unimplemented runtime integration.


## Active task: apply two-bone IK to poses (Codex)

- Branch `codex/pose-ik`, based on #62 `98e0025`; ADR 0058.
- Scope: validate direct chain/unit scales/rest lengths, convert position solution to root/knee local rotations, verify FK and commit outputs transactionally. No terrain or physics mutation.
- Checks: independent targets under rotated parents, aliases, near/exact opposite directions, malformed/unsupported rigs, preservation and native full matrix. Windows/hardware/owner gates remain open.


## Two-bone IK geometry — draft #62

Codex, `codex/two-bone-ik`, stacked on #61. ADR 0057 adds engine `animation::solve_two_bone` in `lykoware/ik.hpp`: double-space root/knee/end positions, no-stretch inner/outer reach clamping, explicit direction/bend-hint fallbacks and transactional errors for invalid or precision-collapsed geometry. No pose, joint rotation or physics mutation; no terrain-foot-placement claim.

Clean unchanged source `9fec63321e6bd067bbd7f9d8e9873b8de80d2de0` passed 67/67 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass. Report `/workspace/lykoware-two-bone-ik-validation/report.json`. Analytical 3D bends, both limb orderings/reach limits, explicit/collinear fallbacks, far origin, 200 varied targets, subnormal normalisation, length boundaries and failure atomicity pass. Four external deliberate faults were caught: removed clamp, ignored hint, ignored lower length and overwritten failure output. No faulty/generated artifacts committed.

Next: validate mapped rest-pose limb lengths/bend planes, convert solved positions to joint rotations, then add terrain probes and foot planting. Master checklist 02.11 split into these steps; only Linux verification checked. Windows/hardware/hosted CI and owner merge remain open. Subsequent handoff edits are documentation only.


## Active task: two-bone IK geometry (Codex)

- Branch `codex/two-bone-ik`, based on #61 `b4f0a8b`; ADR 0057.
- Scope: engine-owned double-coordinate position solver, reach clamping, bend/direction fallbacks, transactional failures and analytical/varied tests. No pose rotation application, physics mutation or terrain placement.
- Acceptance: new geometry checks plus native Debug, ASan/UBSan and clang-tidy profiles; preserve current renderer/player checks. Windows/hardware gates and owner merge remain separate.


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


## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).

`main` at `7616ce3` includes Claude's #32–#34 math/mesh/renderer stack (alongside #31 ECS/physics) and Codex's #35 contract hardening and #36 scenes/command replay. Claude's pending runtime/character stack reaches glTF skin/animation loading (#55). Codex has continued through the CPU reference (#56) and GPU skinning/animated player (#57); main is unchanged. Earlier per-task entries remain in pull requests/git history. Details: [STATUS](STATUS.md).

`main` at `855e01e` includes Claude's #32–#34 math/mesh/renderer stack (alongside #31 ECS/physics). Review follow-ups #35 and #36 subsequently merged as `7616ce3`; their per-branch evidence below is historical. Earlier per-task entries remain in pull requests/git history. Details: [STATUS](STATUS.md).

Owner-supplied vision is captured in [ENGINE_VISION.md](ENGINE_VISION.md).

Owner rules for every task: small stacked PRs; commit small and often; keep all five CI jobs green; record each decision as an ADR; keep STATUS handoff-ready after every task; Joshua merges; no regenerable generated files in commits; artifact upload stays opt-in and no storage or service is bought; no roadmap box ticked unless truly done.

Check latest main and open PRs before starting. Documents do not automatically synchronize assistants or reserve work.

## Active task: GPU skinning and animated player (Codex)

- Owner/status: Codex, implemented in draft [#57](https://github.com/Avrenthrad/lykowareGE/pull/57) on `codex/gpu-skinning`, based on #56 `7eaa2e3`.
- Scope: [ADR 0052](adr/0052-gpu-skinning.md), RHI frame-owned palettes, renderer skinned mesh/pose API, GPU/CPU comparison captures and procedural animated sandbox player. No editor or physics-controller rewrite.
- Verified runtime/test source: `1ef0bbbe45cf9dacf83e593aa009a6f3948f7da5`, clean unchanged source; 65/65 each Debug, ASan/UBSan and clang-tidy 18, no skips; build/staging/installed CLI pass. Report/fingerprint and exact scope in STATUS. Subsequent handoff edits are documentation only.
- New checks: GPU vs CPU captures, independent poses, four influence slots/joint 255, normal transform/singular fallback, arena growth/cap refusal/recovery, malformed IDs, restart, mannequin motion and headless play. Wrong-normal and missing-fourth-influence shader mutations were caught; the first exposed and closed a flat-normal coverage gap.
- Supplemental Debug checks with Khronos 1.4.309 synchronization validation enabled: GPU skinning, Vulkan restart and sandbox play all pass, no skips/errors reported. No validation tool binary was committed.
- Limits: per-part padded palette copies, retained arena capacity, larger static stride in animation-enabled renderers; no crowd/flagship budget claim. Hosted CI fails before steps (42161276822, billing/spend-limit annotation); Windows and target hardware remain unverified. Joshua merges.
- Next: imported character/clip integration and foot IK, then animation/physics blending/ragdoll. No main merge or roadmap exit.

## Active task: CPU skinning reference and rendered pose checks (Codex)

- Owner/status: Codex, implemented in draft [#56](https://github.com/Avrenthrad/lykowareGE/pull/56) on `codex/skinned-rendering`, based on Claude's glTF skins source `15383a27bcc788191bb6646f5d01f27a4673ea47`. Claude's branch remains unchanged.
- Scope: [ADR 0051](adr/0051-cpu-skinning-reference.md), pure CPU deformation API and actual renderer capture checks. No mutable GPU buffer/descriptor/shader changes, editor or frame-loop upload path.
- Baseline checked independently: exact clean Claude snapshot passes **61/61 each** Linux Debug, ASan/UBSan and clang-tidy 18, no skips; configure/build/install/installed CLI pass; source unchanged. Raw local report: `/workspace/lykoware-health-gltf-skins-vulkan/report.json`. The old health-check audio/renderer probes now reject foreign/stale handles.
- Checks completed on clean unchanged runtime source `69612eb853fe6764555951b267edcb65cd19f174`: **63/63 each** Linux Debug, ASan/UBSan and clang-tidy 18, no skips, including existing capture golden, new analytical/alias/failure checks and glTF clip-to-pixel captures with independent poses. Installation/installed CLI pass. Six deliberate faults caught. Report `/workspace/lykoware-health-skinning-reference/report.json`; fingerprint in STATUS. Later handoff edits are documentation only.
- Windows/MSVC and target hardware remain unverified; GitHub's #54 run 42135354689 has five failed jobs with zero steps. No hosted gate was changed. Draft #55 makes Claude's final push reviewable without changing his branch.
- Next after this slice: measured GPU skinning, then animated sandbox character. Phase 1 acceptance/hardware gates stay open; Joshua merges.

## Active task: Phase 2, character feel (Claude)

- Owner/status: Claude, active. PR 1 (follow camera, [ADR 0044](adr/0044-follow-camera.md)) in review as #51 on `claude/follow-camera`, stacked on the capture golden. PR 2 (playable character and obstacle course, [ADR 0047](adr/0047-playable-character.md)) in review as #52 on `claude/playable-character`, stacked on PR 1. PR 3 (drawing between physics steps, [ADR 0048](adr/0048-drawing-between-steps.md)) in review as #53 on `claude/tick-smoothing`, stacked on PR 2. PR 4 (skeletal animation core, [ADR 0049](adr/0049-skeletal-animation-core.md)) in review as #54 on `claude/animation-core`, stacked on PR 3. PR 5 (glTF skins and animations, [ADR 0050](adr/0050-gltf-skins-and-animations.md)) in review as #55 on `claude/gltf-skins`, stacked on PR 4. Next: skinned drawing, an animated character; later foot IK and ragdoll.
- Objective (owner: "Continue", 2026-11-05): Phase 2, character feel on terrain, alongside the owner's reviews of #37–#50.
- Shared interfaces: new `lykoware/animation.hpp`; `gltf::read_skinned`/`load_skinned` (additions to the glTF reader, ADR 0035's module); `tests/support/gltf_builder.hpp`; new `lykoware/follow_camera.hpp` (reads `physics::World::cast_ray`); new `lykoware/player.hpp`; `MouseState::move_x/move_y` and `Window::set_relative_mouse` (additions to the window/input API); the shared course `samples/course/obstacle_course.hpp`; sandbox `--play`.
- Checks: `camera.follow`, `player.course`, `animation.pose`, `assets.gltf_skin`, deliberate breaks, five CI jobs once CI runs again. Feel is the owner's to judge in play.

## Active task: real capture golden (Claude)

- Owner/status: Claude, active. One PR ([ADR 0043](adr/0043-renderer-golden-capture.md)) in review as #50 on `claude/capture-golden`, stacked on scripting PR 2.
- Objective: the Phase 1 exit item "a real image capture passes its golden tolerance".
- Shared interfaces: a committed golden (`tests/golden/falling-crates-160x120.ppm`) and the `lykoware_image_compare` test helper. A deliberate change to the lit shader, camera, clear colour or the falling-crates sample now needs a reviewed golden update in the same PR (`LYKOWARE_UPDATE_GOLDEN=1`).
- Checks: `scene.capture_golden`, deliberate breaks, five CI jobs once CI runs again.

## Active task: scripting (Claude)

- Owner/status: Claude, active. PR 1 (sandboxed Lua 5.4, [ADR 0041](adr/0041-lua-scripting.md)) in review as #48 on `claude/script-lua`, stacked on window lifecycle. PR 2 (scripts drive scenes through recorded commands; `lyko run --script`, [ADR 0042](adr/0042-script-driven-scenes.md)) in review as #49 on `claude/script-commands`, stacked on PR 1.
- Objective (owner's choice, 2026-11-05: "go with what you recommend"): the Phase 1 scripting entry point, in Lua.
- Shared interfaces: new `lykoware/script.hpp`; new vendored `third_party/lua/` (MIT); `lykoware_lua` linked privately into the engine. PR 2 adds `lykoware/scene_script.hpp`, one read-only method on Codex's `scene::Scene` (`position(id)`, ADR 0031's module), and `lyko run --script/--record`.
- Checks: `script.sandbox`, deliberate breaks, a local fuzz run under ASan/UBSan, five CI jobs once CI runs again.

## Active task: window lifecycle (Claude)

- Owner/status: Claude, active. One PR ([ADR 0040](adr/0040-window-lifecycle.md)) in review as #46 on `claude/window-lifecycle`, stacked on GPU timings.
- Objective (owner's choice, 2026-11-05): the Phase 1 exit item "resize/focus/close and restart tests pass".
- Shared interfaces: `Window::has_focus`/`minimized`, `release_keys_and_mouse_buttons`; the swapchain skips frames while minimised.
- Checks: `window.lifecycle`, `rhi.swapchain`, new `lifecycle.restart` and `lifecycle.restart_vulkan`, deliberate breaks, five CI jobs once CI runs again.

## Active task: GPU timings (Claude)

- Owner/status: Claude, active. One PR ([ADR 0039](adr/0039-gpu-timings.md)) in review as #44 on `claude/gpu-timings`, stacked on textures PR 3.
- Objective (owner's choice, 2026-11-05): the Phase 1 exit item "traces report CPU/GPU timings where supported".
- Shared interfaces: `Device::gpu_timestamps`, `RenderTarget::last_gpu_time_ns`, `Swapchain::take_gpu_frame_times`, `CpuTrace::add_gpu_span`/`elapsed_us`; sandbox `--window --vulkan --trace FILE`; `lyko trace --gpu`.
- Checks: GPU timing checks in the RHI tests (lavapipe has timestamps), `sandbox.gpu_trace`, deliberate breaks, five CI jobs once CI runs again.

## Active task: textures (Claude)

- Owner/status: Claude, active. PR 1 (PNG and JPEG decoding through vendored Wuffs, [ADR 0036](adr/0036-image-decoding.md)) in review as #41 on `claude/image-decode`, stacked on model loading. PR 2 (textures in the RHI and renderer, [ADR 0037](adr/0037-gpu-textures.md)) in review as #42 on `claude/gpu-textures`, stacked on PR 1. PR 3 (glTF materials, texture coordinates and mesh parts, [ADR 0038](adr/0038-gltf-materials.md)) in review as #43 on `claude/gltf-materials`, stacked on PR 2.
- Objective (owner's choice, 2026-11-05): textures on models, in three PRs: (1) image decoding, (2) GPU textures in the RHI and renderer, (3) glTF materials and texture coordinates.
- Shared interfaces: new `lykoware/image_decode.hpp`; new vendored `third_party/wuffs/` (MIT option); PR 2 changes RHI pipelines (descriptor sets) and the lit shader.
- Checks: images encoded by the tests (none committed), deliberate breaks, a cross-check against PIL and a fuzz run (local), five CI jobs once CI runs again.

## Active task: model loading (Claude)

- Owner/status: Claude, active. PR 1 (glTF meshes, [ADR 0035](adr/0035-gltf-meshes.md)) in review on `claude/gltf-meshes`, stacked on audio PR 3 to avoid conflicts in shared files.
- Objective (owner's choice, 2026-11-05): our own glTF 2.0 reader; shapes first, textures later.
- Shared interfaces: new `lykoware/mesh.hpp` and `lykoware/gltf.hpp`; `Renderer::add_mesh`; `MeshRenderer` gains a last field `mesh`; sandbox `--model FILE`.
- Checks: models built in tests (none committed), GPU pixel checks, deliberate breaks, a local fuzz run, five CI jobs once CI runs again.

## Active task: audio (Claude)

- Owner/status: Claude, active. PR 1 (mixer and WAV reader, [ADR 0032](adr/0032-audio-mixer.md)) in review as #37 on `claude/audio-mixer`, based on `main` at `7616ce3`. PR 2 (SDL3 output, [ADR 0033](adr/0033-audio-output.md)) in review as #38 on `claude/audio-output`, stacked on PR 1. PR 3 (3D positional sound and sandbox crate knocks, [ADR 0034](adr/0034-spatial-audio.md)) in review on `claude/audio-spatial`, stacked on PR 2. CI is blocked account-wide since 2026-11-05 (jobs fail before a runner starts); checks are local only until the owner restores it.
- Objective (owner's choice, 2026-11-05): sound playback through SDL3 (already vendored), our own mixer, 3D positional sound tied to ECS entities, deterministic tests.
- Plan: PR 1 the mixer (`lykoware/audio.hpp`, no device); PR 2 output through SDL3 on its audio thread, with dummy-driver tests; PR 3 a listener and an ECS sound component, camera-relative, with procedurally made crate landing sounds in the sandbox (no binary assets).
- Shared interfaces: new `lykoware::audio`; PR 2 turns on SDL's audio subsystem in `cmake/VendorSdl3.cmake`; PR 3 adds an ECS component next to `Transform`.
- Checks: exact-value tests with no device; bit-identical output however rendering is split; deliberate breaks; five CI jobs.

## Completed: review follow-ups (Codex, #35 merged)

## Active task: runtime package notices (Codex)

- Owner/status: Codex, implemented as draft #45 on `codex/runtime-package-notices`, based on `main` at `7616ce3`; Windows/hosted CI blocked.
- Scope: install runtime dependency notices/provenance, SDL embedded attributions, readable package index and installed-tree regression checks; ADR 0045. No engine/audio/renderer/asset API changes.
- Coordination: Claude owns audio/model/texture/GPU-timing PRs #37–#44. This is the separate packaging omission noted in #41; preserve those branches and test integration locally without merging them.
- Checks: baseline Linux Debug 42/42 passed; package code `6984cbd` combined locally with Claude head `2c41f62` passes 52/52 (local integration `15c0b33`, no skips). Fresh full/component installs, unchanged copied bytes, normalized excerpt text, CRLF handling, relocation, missing-anchor errors and a deliberately missing Jolt notice are checked.
- Blocker: GitHub refused all five jobs before a runner executed any steps, citing failed account payments or the spending limit (run 42036213824). Windows/MSVC remains unverified; no spend, billing change or weakened checks.
- Merge: Joshua merges reviewed work. The earlier merge instruction covered #35/#36; it is not standing authorization for new PRs.

## Active review follow-ups (Codex)

- Owner/status: Codex, ready for owner review in #35; all five jobs passed on `f113b45` ([run](https://github.com/Avrenthrad/lykowareGE/actions/runs/41826863572)); original base `5b46f0812eab373fa3e3062acd272c6b593a1b63`, branch `codex/core-contract-hardening`.
- Scope: ECS/physics handle isolation, allocation/exception safety, exclusive physics attachment ownership; ADR 0030. No renderer rewrite.
- Affected interfaces: ECS Entity, physics handles, ECS movement/hook claims, physics component accessors.
- Checks: cross-world rejection; move/locked/rebind tests; injected allocation failures; unchanged deterministic physics hashes; full five-job CI.
- Related work: Claude's `claude/math`, `claude/rhi-mesh`, and `claude/render-scene` supply camera-relative rendering. Keep their ownership and changes intact.
- Implemented on #36, verification in progress: stable scene/schema IDs, fixed-tick replay, render/simulation separation, capture/measurement runner and documentation reconciliation.
- Hardware gate: actual Windows GPU timing and memory on the recommended desktop require that machine. A software Vulkan run is not that evidence.
- Integration: Joshua merges, per the owner rules above. Stacked follow-up PRs may be built/tested together locally before owner merge.

## Completed: rendering work (Claude, #32–#34 merged)

Claude owns #32 (`claude/math`), #33 (`claude/rhi-mesh`) and #34 (`claude/render-scene`). They supply camera-relative math, RHI mesh/depth drawing and the first lit ECS falling-crates renderer. Their CI evidence is recorded in those PRs/STATUS. Codex's scene-command integration preserves that stack and #30's physics slot retirement; it does not replace or publish Claude's branches.

## Completed: scene/replay integration (Codex, #36 merged)

- Owner/status: Codex, implemented and ready for review as #36 (`codex/scene-command-replay`), stacked on #35; preserves Claude's #30 and merged #32–#34. Runtime/scene source `26f7332` passed all five CI jobs (run 41827916577); subsequent documentation/build-concurrency checks must stay green.
- Scope: ADR 0031, versioned JSON box scene/command schema, stable authored IDs, fixed-tick dispatch, render-only snapshots, CLI replay/capture/report, local Windows measurement script and documentation reconciliation.
- Verified on `26f7332`: canonical/malformed-input checks; every tick equal at 30/60/144 Hz/irregular frames; unchanged simulation after interpolation/GPU draw; repeated real CLI captures/hash equality; installed shader paths; five CI jobs green.
- Hardware: owner supplied Windows 11 Home / i7-14700F / 16 GB; graphics device/VRAM and actual measurements pending. Do not record personal machine/user identifiers from the screenshot.
- Integration: [REVIEW_FOLLOWUP](REVIEW_FOLLOWUP.md); Joshua merges. No full editor or later-phase gameplay work.

## Completed task: double-precision positions

Owner: Claude. Done: #26 (ADR 0021), #27 and #28 merged. Jolt and the physics API use double-precision positions (ADR 0022, Proposed); the same scene 16 km and 64 km from the origin matches the origin scene and costs the same; kinematic bodies never sleep. Details and CI runs are in [STATUS.md](STATUS.md).

## Completed task: physics spike

Owner: Claude. Done: #21, #23, #24 and #25 merged. Jolt v5.6.0 vendored with cross-platform determinism (ADR 0018); the `lykoware::physics` API (ADR 0020, accepted by the owner on 2026-11-04) with bodies, height-field terrain and walking characters; a body-count benchmark that also found and fixed a step-memory abort. Details and CI runs are in [STATUS.md](STATUS.md).

## Completed task: physics decision (ADR 0003)

Owner: Claude for the review; the owner decided on 2026-11-04 (#20). Jolt v5.6.0; the *Numerical Recipes*-derived functions are replaced by our own; cross-platform determinism on; floats for now.

## Completed task: Phase 1 clocks and fixed-step update

Owner: Claude. Done: #19 merged (ADR 0017, Proposed). `FixedStep` gives numbered fixed ticks (60 Hz default) with no drift and identical results at any frame rate; `FrameClock` measures real frame time; the sandbox animates per tick. The owner has not accepted ADR 0017 or its 60 Hz default.

## Completed task: Vulkan RHI bring-up

Owner: Claude. Done: PRs #11–#14 and #16–#18 merged, with ADR 0013 in #15. First pixels through a Lykoware-owned Vulkan 1.3 RHI (ADR 0002): instance, device, offscreen render target read back into the image harness, swapchain on the SDL window, Slang shaders compiled and validated at build time, graphics pipelines, and the triangle drawn offscreen (pixel-checked) and in the sandbox window. GPU tests run on Mesa lavapipe in Linux CI and report Skipped on Windows runners, which have no Vulkan. The owner has not accepted ADRs 0009–0012 or 0014–0016; merging them did not accept them. Details and CI runs are in [STATUS.md](STATUS.md) and each pull request.

## Active task: local validation evidence (Codex)

- Owner/status: Codex, implemented in draft #47 on `codex/local-validation`, stacked on package PR #45 (`7f754b3`).
- Scope: ADR 0046; local CMake/CTest/staging command, source provenance, honest test/skip reports and failure tests. No engine, audio, model, texture or GPU-timing API changes.
- Related work: Claude retains #37–#44. CI is blocked by account billing/spending settings; this tool gathers local evidence without changing accounts, spending or weakening CI/merge rules.
- Checks: 11 runner tests with failure subcases; exact main 41/41 Debug; combined `48b0da9` 55/55 each Debug, ASan/UBSan and clang-tidy 18, no skips, clean unchanged source. Installation and real scene capture pass. [Health report](HEALTH_CHECK_2026-11-06.md) records two reproduced P2 audio/renderer ownership defects and manual repros. No engine fix applied in this task.
- Handoff: `codex/health-full-stack` is a pushed integration snapshot through Claude #46, not merged main. Fix ownership on owning branches, restore CI through the owner, run actual Windows Debug/Release and hardware budgets. [Runner guide](LOCAL_VALIDATION.md). Joshua retains merge ownership; Phase 1 stays open.

## Task template

Copy into an issue or PR before implementation:

- Task/module:
- Owner and status (planned / active / blocked / review / done):
- Base commit and branch:
- Objective:
- Scope and affected paths:
- Shared interfaces/formats affected:
- Related tasks/PRs and dependencies:
- Acceptance checks:
- Assumptions and decisions needed:

## Handoff template

Use in the task/PR when pausing, completing, or transferring work:

- Task, branch, commit, PR:
- Completed behavior:
- Files/interfaces changed:
- Checks run and actual results (link CI where available):
- Unverified behavior/platforms:
- Open decisions and blockers:
- Next concrete action:
- Checklist/documentation updates:

## Recorded task: accept ADR 0002 and ADR 0004

- Task/module: Phase 0 documentation. Accept the graphics and data-format ADRs and correct the brief.
- Owner and status: done. The content reached `main` through the merged #5–#8 stack; [PR #4](https://github.com/Avrenthrad/lykowareGE/pull/4) itself was closed. No runtime owner.
- Base commit and branch: `850bd50451ecf108e353716fca24db5a685ddd00` on `main`; branch `docs/accept-adr-0002-0004`.
- Objective: Record Vulkan 1.3 behind a Lykoware-owned RHI, and canonical JSON / TOML / glTF 2.0, and make the brief agree.
- Scope and affected paths: documentation only. ADRs 0002 and 0004, the ADR index, the brief, STATUS, this board, and the planning pages that still described the old proposals.
- Shared interfaces/formats affected: graphics API direction and authored-data encodings. No schema, renderer, parser or vendored source.
- Related tasks/PRs and dependencies: ADR 0001 is accepted. ADR 0003 stays proposed. wgpu, ash and RON are rejected as Rust-oriented.
- Acceptance checks: the brief, ADR index, STATUS and this board name the same decisions; no code, dependency or spend; Windows and Linux CI still build the existing foundation.
- Assumptions and decisions needed: shader-compiler licence review and a software Vulkan driver such as Mesa lavapipe in CI are follow-ups, not accepted requirements. D3D12 only if Intel Arc measurements justify it. Parser libraries are chosen when first needed. Jolt is not selected by the brief's native-C++ wording.

### Handoff

- Task, branch, commit, PR: accept ADR 0002 and ADR 0004; branch `docs/accept-adr-0002-0004`; decision commit `0d9100ff73271a3828eaae55951a2f43a203d112`; pull request https://github.com/Avrenthrad/lykowareGE/pull/4.
- Completed behavior: ADR 0002 and ADR 0004 are Accepted on this branch. The brief no longer evaluates wgpu against ash, no longer puts Jolt behind bindings, and no longer lists RON as a format.
- Files/interfaces changed: documentation listed in the task. No public API or on-disk schema was implemented.
- Checks run and actual results: relative links in the edited Markdown resolve. Local Linux `cmake --preset linux-debug`, `cmake --build --preset linux-debug`, and `ctest --preset linux-debug` passed (`foundation.smoke`). Pull-request CI [run 41325777090](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325777090) on `d3c4e4535f698b31b7acdd212cabbe9b69d98ca3`: Linux portability check passed. Windows Debug and Release configure, build, test, stage, and staged executable passed. Both Windows jobs then failed on `actions/upload-artifact` because the account artifact storage quota is exhausted. No storage was purchased. That quota is not freed immediately by deleting artifacts.
- Unverified behavior/platforms: no renderer, capture path, parser round-trip or vendored library was exercised. The Windows failure is the artifact upload, not the build or test.
- Open decisions and blockers: ADR 0003 physics; multiplayer and hardware profiles; shader-compiler licence; lavapipe (or another software Vulkan driver) as a CI follow-up; exact parser choice at first use.
- Next concrete action (at the time): Phase 1 window/input/timing. Superseded by the owner order under "Suggested next task". Graphics dependencies are vendored with their first caller in the Vulkan RHI task. ADR 0003 stays open.
- Checklist/documentation updates: graphics and data-format acceptance is checked. Physics acceptance stays open. STATUS next steps match this handoff.

## Suggested next task

Owner order, set 2026-11-02: (1) Vulkan RHI bring-up, now done; (2) Phase 1 clocks and fixed-step update, now done; (3) decide Jolt in ADR 0003 before any physics, now decided, with the spike as the active task above; (4) agree hardware profiles, multiplayer intent, world scope and budgets, now decided in ADR 0021 (desktop only, RTX 4070 recommended at 1440p60, 16 GB RAM, single-player built co-op-ready, 16 km+ world, double precision). Leave ADR 0008 Proposed until the owner accepts it. No full editor or content suite work before the roadmap gates.

## Codex handoff: review follow-ups

- PR #35: `codex/core-contract-hardening`; source `f113b45` passed all five profiles. Following `6a1f299` updates documentation only.
- PR #36: `codex/scene-command-replay`; runtime/scene source `26f7332` passed all five profiles; subsequent commit records that evidence and adds the two-job host-tool build limit for the owner's 16 GB system. See current PR checks for the exact final head.
- Local: 41/41 CTests pass with Mesa lavapipe (LLVM19), including actual capture; installed replay/capture works from `/tmp`. Broken-link/conflict-marker checks pass. Job count 0 is rejected; 2 configures successfully; CI cache fallback preserves exact tool version/recipe/platform selection.
- No paid services, artifact uploads, generated build files or personal host/user identifiers committed. Upstream JSON source is intentionally vendored with its MIT licence/provenance and staged notice.
- Next: Joshua merges reviewed PRs in dependency order; run `tools/validation/measure-scene.ps1` on the Windows PC and inspect the local reports. Hardware profile/frame pacing/VRAM remain unverified; no Phase 1 exit or flagship budget is claimed.
