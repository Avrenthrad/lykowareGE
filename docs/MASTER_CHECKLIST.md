# Lykoware master completion checklist

Owner-requested living checklist, saved 6 November 2026. Permanent entry point: README → Start here.

**Finish target:** a reliable engine, custom editor and content tools that can build and ship the flagship game. Maintenance continues after release. This checklist captures the full planning list; detailed scope/exit criteria remain in [ROADMAP](ROADMAP.md). Verified implementation lives in [STATUS](STATUS.md); active ownership in [WORK_BOARD](WORK_BOARD.md). The existing [build checklist](../CHECKLIST.md) retains its historical verified milestones.

## How every contributor maintains this

- Update affected entries in the same PR/handoff as the work. Add evidence and remaining gates.
- `[ ]` means the complete acceptance requirement is still open. Use notes such as **implemented/merged; broader acceptance separate**, **partial**, or **blocked** for progress within an open entry.
- `[x]` requires the whole stated requirement to be verified and accepted under the repository's merge/release rules. Code in an unmerged PR does not complete a phase.
- Split broad entries into concrete substeps when work starts; preserve stable IDs, record replacements and explain scope changes. Never silently delete unfinished requirements.
- Keep this checklist, CHECKLIST.md, ROADMAP and STATUS consistent. Do not tick Windows, hardware, legal or owner acceptance from Linux-only evidence.

## Current position

**Animation stack integrated.** #71, #72 and #73 were reviewed, re-verified together (82/82 in each local Linux profile) and merged at the owner's request on 10 November 2026 as `35ad909`, `58e170e` and `6cabcf3`; the [project checkpoint](PROJECT_CHECKPOINT.md) has the details. The selected [15-item batch](NEXT_15_BATCH.md) is **15/15 integrated**. Hardware, owner-feel, performance and public-release gates stay open, and partial parent items stay unchecked.

**Locomotion feel, 13 November 2026.** #84, #85 and #86 merged with all five hosted checks green. 02.17c and 02.17e are ticked below. 02.17d: stride matching, lean, gait blend, pivot hold and the profile `locomotion` key merged with #86; start/stop/pivot clips and imported clip speeds with #89. The remainder merged with #89, so 02.17 is ticked; owner feel stays under 02.16. Ragdoll parts 02.12b–d may start. Remaining engine work is split in [docs/assignments](assignments/COORDINATION.md).

## Earlier ten-step batch (historical)

**[Stationary terrain alignment — ten checked local steps](FOOT_ALIGNMENT_BATCH.md)**, draft #64. Source `2cc97b5` passed 68/68 each Linux Debug, ASan/UBSan and clang-tidy 18, no skips, plus synchronization-validation CLI. This does not complete the broad foot-IK milestone or its Windows/merge/hardware gates. Next: reusable runtime chains/scratch, then foot orientation/stance/body constraints and broader terrain tests.

## Completed verification substeps

These checked items certify the stated local verification runs only; their parent features and merge/Windows gates above remain open.

- [x] **02.02a-Linux** Verify visual fitting on source `1aab213`: 66/66 each Debug, ASan/UBSan and clang-tidy 18, no skips; separate synchronization-validation CLI check passes.
- [x] **02.03a-Linux** Verify authored profiles on source `c469c81`: 66/66 each of the same profiles, no skips; separate synchronization-validation CLI check passes.
- [x] **02.02b-Linux** Verify role mapping on `1699fc8`: 66/66 each Debug, ASan/UBSan and clang-tidy 18, no skips; separate synchronization-validation playback passes.
- [x] **02.11a-Linux** Verify IK geometry on `9fec633`: 67/67 each Debug, ASan/UBSan and clang-tidy 18, no skips; four deliberate geometry/atomicity faults caught.
- [x] **02.11b-engine-Linux** Verify pose application on `fbf7468`: 67/67 each Debug, ASan/UBSan and clang-tidy 18, no skips; near-180 regression fixed and four deliberate pose faults caught.
- [x] **02.11c-stationary-Linux** Complete the ten-step stationary vertical foot-joint batch on `2cc97b5`: 68/68 each native Linux profile, no skips; synchronization CLI and five external faults checked.
- [ ] **02.02a/02.03a-Windows** Run equivalent Windows Debug/Release and actual hardware checks.
- [x] **02.02a/02.03a-Merge** Complete review, required CI and owner-authorized merge for #59/#60 — integrated by #65.

## 1. Consolidate the current engine

- [x] **01.01** Review pending PRs in dependency order.
- [x] **01.02** Resolve review findings and integration conflicts.
- [x] **01.03** Restore hosted CI through the account owner.
- [x] **01.04** Pass Windows Debug/Release and all Linux checks — hosted run 42386213427; Windows Vulkan skips remain a hardware gate.
- [x] **01.05** Merge approved work in dependency order.
- [x] **01.06** Verify a fresh checkout builds and installs.
- [x] **01.07** Reconcile STATUS, roadmap, checklist and ADRs.
- [x] **01.08** Establish repeatable Windows testing — hosted Debug/Release and local validator/measurement instructions; target-PC execution is separate.
- [ ] **01.09** Confirm target GPU and VRAM — RTX 4070 owner-reported; VRAM/driver still unmeasured.
- [ ] **01.10** Measure frame times, hitches, memory and GPU usage.
- [ ] **01.11** Agree measurable quality/performance budgets.
- [ ] **01.12** Close remaining Phase 1 acceptance gates.

Hardware evidence tooling merged in [#66](https://github.com/Avrenthrad/lykowareGE/pull/66), `32261bf`, ADR 0060. Exact source `e803335` passed all five hosted jobs; local Debug 71/71 and nine assessor tests passed. The four parent acceptance gates remain open pending actual PC evidence.

- [x] **01.09a-tooling** Capture optional NVIDIA name/VRAM/driver inventory and source/executable/fixture provenance; test >4 GiB values and failed driver queries in both Windows PowerShell hosts.
- [x] **01.10a-tooling** Validate three offscreen runs, raw duration distributions, physical-device flags and consistent replay/device/configuration; preserve incomplete/failed evidence.
- [x] **01.11a-tooling** Enforce explicitly supplied provisional fixture-bound budgets; test equality, overruns, malformed policies and stale-success replacement. This does not agree or calibrate the target-PC budgets.

## 2. Character and terrain slice

- [x] **02.01** Define character import conventions and validation — #69, shared CPU inspection/profiles; native 76/76 each and all five hosted jobs.
- [ ] **02.02** Add rig inspection, scale fitting and capsule alignment.
  - [x] **02.02a** Visual bind-height inspection and explicit uniform height/feet-origin fitting — **implemented/merged; broader acceptance separate**, [#59](https://github.com/Avrenthrad/lykowareGE/pull/59), source `1aab213`; Merged in #65; later #68 source passed all hosted profiles. Actual target-PC checks remain separate.
  - [x] **02.02b** Authored profile and humanoid bone-role mapping/inspection — **implemented/merged; broader acceptance separate**, #60/#61; unique names and leg ancestry validated. Limb lengths, bend planes and IK remain separate.
  - [x] **02.02c** Orientation correction and capsule/camera alignment — #69, visual yaw, explicit collision/pivot dimensions and render/IK parity.
  - [x] **02.02d** Retargeting and rig compatibility diagnostics — #69, declared unique-name/topology/unit-scale/rest-direction/axis subset; pose transfer remains 02.10.
- [ ] **02.03** Configure clips through authored data.
  - [x] **02.03a** Model/idle/walk/visual-height JSON profiles — **implemented/merged; broader acceptance separate**, [#60](https://github.com/Avrenthrad/lykowareGE/pull/60), source `c469c81`; Merged in #65; all hosted profiles pass in #68. Actual target-PC checks remain separate.
  - [x] **02.03b** Extend authored configuration to mapped rig roles and remaining locomotion states — #61/#68 map roles and all six sample clip states; authored general graphs remain 02.08.
- [ ] **02.04** Add idle, walk, run, jump, fall and landing states. **Partial; runtime implementation merged #68:** six-state presentation sample, original demonstration clips and optional imported selectors; finished character art/owner feel remain separate.
  - [x] **02.04-runtime** Verify and merge six-state controller/Jolt bridge and profile selectors — #68; all five CI profiles and 74/74 each local Linux profile.
- [ ] **02.05** Blend transitions without snapping. **Partial; runtime implementation merged #68:** owned interruptible pose crossfade with source capture; foot/body constraints remain separate.
  - [x] **02.05-crossfade** Verify and merge generic crossfade and sample state transitions — #68; interruption, aliasing, allocation failures and runtime integration verified.
- [x] **02.06** Handle turning, slopes, steps and uneven ground — #70 bounded/interpolated facing and real course tests; subjective feel remains 02.16.
- [x] **02.07** Decide root motion versus controller-driven movement — ADR 0064 / #70, explicit controller authority; root tracks visual-only.
- [x] **02.08** Add reusable animation graphs or equivalent configuration — implemented/verified #71, owned bounded graph, native/sandbox/CLI callers; merged as `35ad909`.
- [x] **02.09** Add animation events and attachment sockets — implemented/verified #71, interval/wrap/partition/capacity and corrected-pose socket tests; merged as `35ad909`.
- [x] **02.10** Retarget between supported rigs — implemented/verified #71, strict named topology/rest-axis subset, real glTF transfer, lengths/axis/overlap/256-joint/refusal tests; merged as `35ad909`.
- [ ] **02.11** Add foot placement and foot IK.
  - [x] **02.11a** Two-bone position solver — **implemented/merged; broader acceptance separate**, [#62](https://github.com/Avrenthrad/lykowareGE/pull/62), source `9fec633`; no stretch, reach/fallback flags and transactional failures.
  - [ ] **02.11b** Validate mapped rest-pose limb lengths/bend planes and apply solved joint rotations.
    - [x] **02.11b-engine** Validated direct-chain pose operation — **implemented/merged; broader acceptance separate**, #63 `fbf7468`; unit scales only, no helper chains.
    - [x] **02.11b-integration** Bridge mapped roles, rest/bend calibration and actor-world/model coordinates — #64/#69 common direct-chain calibration, fitted/yawed/far-origin mapping; later all-profile validation in #71. Broader rigs remain excluded.
    - [ ] **02.11b-runtime** Prepare validated chains/reusable scratch and measure runtime costs. **Partial:** prepared chains, bounded scratch and retained controller/body buffers merged in #67 (ADR 0061). All five hosted jobs and 72/72 each local Linux profile passed. 128 varied cases match the reference exactly; 1,000 prepared solves record zero ordinary C++ heap allocations versus 7,000 in the reference fixture. Target-PC/crowd budgets remain separate.
      - [x] **02.11b-runtime-prepared** Integrate prepared direct-chain solver and retained foot-placement storage — merged #67; source `6c5626c`, all five CI jobs and 72/72 each native Linux profile.
      - [ ] **02.11b-runtime-scale** Profile representative rigs/crowds and remaining animation/palette allocations on target hardware.
  - [x] **02.11c** Terrain probes, foot planting, offsets and temporal smoothing. Stationary placement merged #64; orientation/normal smoothing (#72, `58e170e`), walking position/normal anchors (#73, `6cabcf3`), planted heading (#75, `635aec5`) and drawn-support resolution (#76) are merged. The owner's feel review stays under 02.16.
    - [x] **02.11c-normals** Apply bounded/smoothed surface orientation to stationary feet — verified #72 `e21f797`, 81/81 each native profile and all five hosted checks; merged as `58e170e`.
    - [x] **02.11c-locks** Authored walking plant/lift cycles and position/normal anchors — verified #73 runtime `b7dbd153`,82/82 each native profile and all five hosted checks. Real translating/rotating/tilting supports pass; merged as `6cabcf3`. Heading/interpolation remain separate.
    - [x] **02.11c-heading** Planted feet keep their heading, carried with the support and bounded — ADR 0068; 83/83 each native profile and all five hosted checks; merged #75 as `635aec5`.
    - [x] **02.11c-drawn** Feet resolve against moving supports as drawn between steps, not their latest physics state — ADR 0069; 84/84 each native profile and all five hosted checks; merged with #76.
  - [x] **02.11d** Integrate pelvis/body constraints and verify varied terrain/character rigs. Bounded bilateral visual pelvis correction merged with #72/#73; the one-foot pelvis merged with #77; rig acceptance merged with #78; the balance shift merged with #80. The owner's feel review stays under 02.16.
    - [x] **02.11d-pelvis** Apply bounded visual pelvis compensation through posed parents with bilateral stance failure/reset behavior — verified #72/#73, full native/hosted checks and review passed; merged as `58e170e` and `6cabcf3`.
    - [x] **02.11d-single** One planted foot lowers the pelvis to reach while walking; the swing foot keeps its animated place — ADR 0070; 85/85 each native profile and all five hosted checks; merged with #77.
    - [x] **02.11d-rigs** Foot features pass on varied rigs (size, proportions, bent rest, bone-aligned frames, extra joints, centimetre units) — ADR 0071; `animation.rig_acceptance`, 8 rigs × 5 checks, 8 of 8 deliberate breaks caught, 86/86 each native profile and all five hosted checks; merged with #78.
    - [x] **02.11d-lean** The body's weight moves over the support: the pelvis shifts sideways over the planted foot on lower ground — ADR 0072; `animation.balance_shift`, a sixth check on all eight rigs, 20 of 20 deliberate breaks caught, 87/87 each native profile and all five hosted checks; merged with #80. A spine tilt is not included.
- [x] **02.12** Add ragdolls and animation/physics blending. Joints #81, mass and pose drives #99, ragdoll from a skeleton #102, blending #104 (`588f8c1`); ADRs 0073, 0078–0080.
  - [x] **02.12a** Joints between physics bodies (fixed, hinge, swing-twist) behind the owned API — ADR 0073; `physics.joints`, 29 of 29 deliberate breaks caught, 88/88 each native profile and all five hosted checks; merged with #81.
  - [x] **02.12b** Body mass, and moving a body to a pose. Evidence: #99 (`1e65373`), ADR 0078, `physics.body_pose`.
  - [x] **02.12c** Build a ragdoll from a skeleton (one body per bone, joints with limits) and read a pose back from it. Evidence: #102 (`e81568b`), ADR 0079, `physics.ragdoll`.
  - [x] **02.12d** Blend between animation and physics, in the sample and replay. Evidence: #104 (`588f8c1`), ADR 0080, `physics.ragdoll_blend` (30/60/144 Hz).
- [x] **02.13** Add stumble, recovery and active ragdoll. Evidence: ADR 0081, `ragdoll::ActiveRagdoll`, `physics.ragdoll_active` (stumble and fall both leave and return; fixed seed 213).
- [x] **02.14** Refine first/third-person camera transitions. Evidence: #96 (89dc40e), ADR 0075, `follow_camera` test.
- [ ] **02.15** Validate character feel on Windows hardware.
- [ ] **02.16** Complete character correctness and performance gates.
- [x] **02.17** Locomotion feel for players and NPCs (owner request, 12 November 2026; ADR 0074). Added after 02.16 was numbered; done before 02.12b–d by the owner's order.
  - [x] **02.17a** ADR: quality bar, sharpness versus weight, directional fluidity, agility profiles, the animation–physics contract and the legal line — ADR 0074 (Proposed); merged with #82.
  - [x] **02.17b** The motor: acceleration, braking, turn rate by speed, gaits, plant-and-pivot and agility profiles in data, with feel checks in `player.course` — `locomotion.motor`, `player.locomotion` and the course with the motor; 50 of 50 deliberate breaks caught; 90/90 each native profile and all five hosted checks; merged with #83.
  - [x] **02.17c** Directional fluidity: continuous heading, mid-stride redirects, strafe and backward movement, a direction-change fixture — `Request::facing`, `strafe_scale` / `backward_scale`, `State::travel`, `player.directions`, sandbox `--agility` / `--locomotion` / `--directions`; merged with #84 (`5cdca44`); all five hosted checks passed on run 43338816385.
  - [x] **02.17d** Animation on the motor: stride matching, lean, starts, stops and pivots, with feet planted. Stride matching (stance slide under 0.12 m/s at 1.6–7 m/s), gait blend, pivot hold, lean and the profile `locomotion` key merged with #86 (`bb30510`). Start, stop and pivot clips from the leg state (fallback blend when absent; placeholder clips in `animation.leg_clips`) and profile `walk_clip_speed`/`run_clip_speed` reported by `lyko inspect-character` merged with #89 (`7e67a06`); all five hosted checks green.
  - [x] **02.17e** NPCs on the same controller with their own agility — `NpcCharacter` and `npc.locomotion` (player and NPC match tick for tick); 11 of 11 deliberate breaks caught; merged with #85 (`fab58b0`); all five hosted checks passed on run 43339040099. A sandbox NPC was not part of this item.

## 3. Combat slice

- [ ] **03.01** Define a small combat scenario.
- [ ] **03.02** Add input buffering and action/state management.
- [ ] **03.03** Add targeting, aiming and combat cameras.
- [ ] **03.04** Add attacks, blocking, dodging and interruption.
- [ ] **03.05** Add hit detection and damage events.
- [ ] **03.06** Add health, stamina and status effects.
- [ ] **03.07** Synchronise animation, hit windows, sound and VFX.
- [ ] **03.08** Add enemy perception, navigation and combat decisions.
- [ ] **03.09** Add equipment and weapon attachments.
- [ ] **03.10** Automate combat scenarios.
- [ ] **03.11** Validate responsiveness, readability and performance.

## 4. Construction and crafting slice

- [ ] **04.01** Define items, resources and recipes.
- [ ] **04.02** Add inventory and crafting.
- [ ] **04.03** Add placement, snapping and previews.
- [ ] **04.04** Validate placement and collision.
- [ ] **04.05** Add structural support and load rules.
- [ ] **04.06** Add damage, destruction and controlled collapse.
- [ ] **04.07** Persist construction through save/load.
- [ ] **04.08** Test large structures and failures.
- [ ] **04.09** Complete building performance gates.

## 5. Strategy and battle slice

- [ ] **05.01** Add selection and RTS camera controls.
- [ ] **05.02** Add unit orders and command queues.
- [ ] **05.03** Add group movement and formations.
- [x] **05.04** Scale navigation and avoidance.
  - GPT: ADR 0081 seeded conservative reservations; 256 agents × 32 ticks, repeated bitwise equality, swept separation and deliberate overlap rejection pass locally. Merged #98 as `d29be31`; all five hosted checks passed on `38e307c`. Local ASan/UBSan also pass; timing budget remains open under `01.11` and `05.09` (**Joshua runs this**).
- [ ] **05.05** Add faction affiliation and hostility.
- [ ] **05.06** Add tactical AI and battle objectives.
- [ ] **05.07** Add simulation, animation and rendering detail levels.
- [ ] **05.08** Test increasingly large battles.
- [ ] **05.09** Establish supported unit-count budgets.
- [ ] **05.10** Validate direct-control/command-view transitions.

## 6. Seamless world streaming

- [ ] **06.01** Define coordinates, regions and partitioning.
- [ ] **06.02** Add asynchronous loading and cancellation.
- [ ] **06.03** Track asset dependencies, residency and eviction.
- [ ] **06.04** Stream terrain and world chunks.
- [ ] **06.05** Stream collision, navigation, audio and entities.
- [ ] **06.06** Persist changes in unloaded regions.
- [ ] **06.07** Version world/save data.
- [ ] **06.08** Render distant worlds with detail transitions.
- [ ] **06.09** Test rapid travel, teleports and memory pressure.
- [ ] **06.10** Eliminate avoidable shader/pipeline compilation hitches.
- [ ] **06.11** Measure throughput and frame pacing.
- [ ] **06.12** Complete Phase 0–6 engine-core acceptance.

## 7. Shared runtime systems — alongside the slices

- [ ] **07.01** Add asset identifiers, dependency tracking and cooked packages.
  - GPT: ADR 0082 stable IDs/edges, standalone cook CLI and source-free C++ byte-package loader; deterministic cook, stale/missing dependency and atomic failure tests pass locally. Hosted CI/merge pending.
- [ ] **07.02** Add import diagnostics and incremental rebuilds.
- [ ] **07.03** Complete flagship lighting, shadows, materials, transparency and post-processing.
  - [x] **07.03a** A stated directional light and ambient, set per renderer. Evidence: ADR 0082, `Renderer::set_lighting`, `render.lighting` (five lights within 2/255 of the formula; defaults byte-identical to the first renderer).
  - [x] **07.03b** Shadows from the directional light. Evidence: ADR 0084, `rhi::ShadowMap`, `Renderer::enable_shadows`, `render.shadows` (ground under a cube keeps only ambient within 2/255, open ground and cube top fully lit, a slanted light moves the shadow, off is byte-identical). Skinned shadows are a later slice.
  - [x] **07.03c** Materials beyond base colour (roughness, metalness, normal maps). Evidence: ADR 0085, `Renderer::enable_materials`, `render.materials` (four materials within 3/255 of the formula on two faces; smooth vs rough highlight, metal tint, normal maps tilted in x and y, metal-roughness texture; plain parts within 1/255 of lit; off and disabled byte-identical; shadows received). Skinned materials are a later slice.
  - [x] **07.03d** Transparent surfaces, sorted and blended. Evidence: ADR 0086, `MeshDraw::pipeline`, `BlendMode::Alpha`, `Renderer::enable_transparency`, `render.transparency` (two panes over a wall within 2/255 of a far-to-near CPU blend; near-first and far-first ECS order give the same image; off, opacity 1 and disabled byte-identical; opacity 0 and a pane behind the wall leave the wall exact; blends the same with materials and shadows on). Transparent shadows and materials are a later slice.
  - [ ] **07.03e** A post-processing pass (tone mapping first).
- [ ] **07.04** Add visibility, batching and GPU resource management.
- [ ] **07.05** Add required terrain, foliage, water and environmental rendering.
- [ ] **07.06** Add UI, text, fonts, localisation and accessibility foundations.
- [ ] **07.07** Complete audio mixing, spatialisation, streaming and prioritisation.
  - GPT #106: ADR 0083 bounded PCM16LE ring streaming, cancellation and nearest-distance voice admission. Tests/hosted CI/merge pending; target-PC listening/scheduling: **Joshua runs this**.
- [x] **07.08** Generate navigation and update it dynamically.
  - [PR #94](https://github.com/Avrenthrad/lykowareGE/pull/94) squash-merged as `f3aa5ec`; all five hosted checks passed on `0c5e205`. ADR 0076: deterministic planar routes, radius clearance and solid updates; route and deliberate wrong-shortcut tests.
- [x] **07.09** Complete jobs, threading, scheduling and resource budgets.
  - [PR #95](https://github.com/Avrenthrad/lykowareGE/pull/95): bounded owner-fenced queue, joined shutdown, exception counts and lifetime tests (ADR 0077); local lifetime, deliberate failure and ASan/UBSan checks passed; all five hosted checks passed on `94609c3`; squash-merged as `9218589`. Explicit limits only; hardware budgets remain **Joshua runs this** under `01.11`.
- [ ] **07.10** Add save migrations, recovery and corruption handling.
- [ ] **07.11** Complete input rebinding and controller support.
- [ ] **07.12** Complete logging, crash reports, profiling and diagnostic capture.
- [x] **07.13** Add replay/determinism where required.
  - [PR #90](https://github.com/Avrenthrad/lykowareGE/pull/90) squash-merged as `0d1aa5f`; all five hosted checks passed on `5551616`. Lua scene command production, position reactions and seeded randomness match at 30/60/144 Hz; omitted-tick probe detects divergence. Future systems require their own replay tests.
- [x] **07.14** Maintain co-op boundaries; implement/test networking if committed to release scope.
  - [PR #88](https://github.com/Avrenthrad/lykowareGE/pull/88): concatenated box-command replay at 30/60/144 Hz and deliberate mutated-stream rejection; all five checks passed on `1641ac3` ([run 43347414176](https://github.com/Avrenthrad/lykowareGE/actions/runs/43347414176)). Networking was not implemented because it is not committed (ADR 0021).
- [ ] **07.15** Audit dependencies, licences and installed packages.

## 8. Custom editor — Track E, after the core gate

- [ ] **08.01** Establish shared serialisable commands.
- [ ] **08.02** Expose commands through CLI and scripting.
- [ ] **08.03** Build editor shell on Lykoware.
- [ ] **08.04** Add viewport navigation and selection.
- [ ] **08.05** Add scene tree, inspector and transforms.
- [ ] **08.06** Add undo/redo and transactional editing.
- [ ] **08.07** Add project management and asset browsing.
- [ ] **08.08** Add prefabs and reusable scene composition.
- [ ] **08.09** Add hot reload and play/debug workflows.
- [ ] **08.10** Add terrain, building and streaming tools.
- [ ] **08.11** Add animation, physics and navigation debugging.
- [ ] **08.12** Add profiling and build/package interfaces.
- [ ] **08.13** Give AI agents access to the same commands.
- [ ] **08.14** Verify equivalent UI/CLI/script results.
- [ ] **08.15** Have an agent build and validate a level headlessly.

## 9. Content suite — Track C, after E2

- [ ] **09.01** C1: import inspection, primitives, diagnostics and modifiers.
- [ ] **09.02** C2: polygon modelling, cleanup, retopology and LOD generation.
- [ ] **09.03** C3: sculpting, UV tools, material painting and baking.
- [ ] **09.04** C4: rigging, skinning, animation editing and mocap retargeting.
- [ ] **09.05** C5: VFX, terrain authoring and audio editing.
- [ ] **09.06** Make every operation command/script accessible.
- [ ] **09.07** Document editable source formats.
- [ ] **09.08** Test import/export round trips and reproducibility.
- [ ] **09.09** Validate each stage against flagship assets.

## 10. Living Memory and factions

- [ ] **10.01** Define persistent NPC identities and life goals.
- [ ] **10.02** Add relationships, reputation and remembered events.
- [ ] **10.03** Add faction knowledge and rumour propagation.
- [ ] **10.04** Add alliances, defection, territory and world changes.
- [ ] **10.05** Define unloaded-region simulation.
- [ ] **10.06** Add authoring, inspection and debugging tools.
- [ ] **10.07** Test consequence consistency and save/load.
- [ ] **10.08** Obtain planned patent/legal review before public release.

## 11. Constrained local world AI

- [ ] **11.01** Benchmark local models on agreed hardware.
- [ ] **11.02** Budget inference alongside rendering.
- [ ] **11.03** Define authored lore/story constraints.
- [ ] **11.04** Control access to world knowledge.
- [ ] **11.05** Add structured planning and dialogue interfaces.
- [ ] **11.06** Validate proposed world actions.
- [ ] **11.07** Handle unavailable, slow or invalid inference.
- [ ] **11.08** Add privacy controls and content safeguards.
- [ ] **11.09** Test long playthroughs, consistency and recovery.
- [ ] **11.10** Verify offline operation and performance.

## 12. Story and consequences

- [ ] **12.01** Define quest, dialogue and story-state formats.
- [ ] **12.02** Add conditions, triggers, objectives and rewards.
- [ ] **12.03** Add branching choices and persistent consequences.
- [ ] **12.04** Integrate relationships, factions and simulation.
- [ ] **12.05** Protect authored story requirements.
- [ ] **12.06** Build authoring/debugging tools.
- [ ] **12.07** Automate branch traversal and progression checks.
- [ ] **12.08** Test save compatibility and blocked-progression recovery.

## 13. Flagship vertical slice

- [ ] **13.01** Lock creative scope and acceptance criteria.
- [ ] **13.02** Establish art, audio and writing style guides.
- [ ] **13.03** Produce original environments, characters and animation.
- [ ] **13.04** Integrate combat, construction, strategy and world systems.
- [ ] **13.05** Integrate representative story and consequences.
- [ ] **13.06** Establish AI-assisted content production workflow.
- [ ] **13.07** Playtest the complete experience.
- [ ] **13.08** Fix usability, quality and performance issues.
- [ ] **13.09** Prove repeatable production with engine and tools.

## 14. Production readiness

- [ ] **14.01** Define supported hardware and operating systems.
- [ ] **14.02** Run long-duration stability and memory tests.
- [ ] **14.03** Test representative AMD, Intel and NVIDIA hardware.
- [ ] **14.04** Test installation, updates, packaging and clean machines.
- [ ] **14.05** Validate accessibility, localisation and settings.
- [ ] **14.06** Establish save migration/compatibility policy.
- [ ] **14.07** Harden scripting, imports and file handling.
- [ ] **14.08** Complete security, dependency and licence reviews.
- [ ] **14.09** Document APIs and development workflows.
- [ ] **14.10** Establish versioning, releases and regression policy.
- [ ] **14.11** Build a new project using the documented starter template.
- [ ] **14.12** Complete owner acceptance and required Pax/public-release reviews.

## 15. Separate Lykoleon projects — own roadmaps

- [ ] **15.01** Howler: Initiation decompiler and upscaler.
- [ ] **15.02** Pax anticheat threat model and implementation roadmap.
- [ ] **15.03** Optional replay/highlight capture.
- [ ] **15.04** Broader platforms/additional graphics backends when justified.
