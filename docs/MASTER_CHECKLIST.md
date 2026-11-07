# Lykoware master completion checklist

Owner-requested living checklist, saved 6 November 2026. Permanent entry point: README → Start here.

**Finish target:** a reliable engine, custom editor and content tools that can build and ship the flagship game. Maintenance continues after release. This checklist captures the full planning list; detailed scope/exit criteria remain in [ROADMAP](ROADMAP.md). Verified implementation lives in [STATUS](STATUS.md); active ownership in [WORK_BOARD](WORK_BOARD.md). The existing [build checklist](../CHECKLIST.md) retains its historical verified milestones.

## How every contributor maintains this

- Update affected entries in the same PR/handoff as the work. Add evidence and remaining gates.
- `[ ]` means the complete acceptance requirement is still open. Use notes such as **implemented/in review**, **partial**, or **blocked** for progress within an open entry.
- `[x]` requires the whole stated requirement to be verified and accepted under the repository's merge/release rules. Code in an unmerged PR does not complete a phase.
- Split broad entries into concrete substeps when work starts; preserve stable IDs, record replacements and explain scope changes. Never silently delete unfinished requirements.
- Keep this checklist, CHECKLIST.md, ROADMAP and STATUS consistent. Do not tick Windows, hardware, legal or owner acceptance from Linux-only evidence.

## Current position

PR [#65](https://github.com/Avrenthrad/lykowareGE/pull/65) merged all runtime/character work #37–64 with packaging and local validation as `aacff25`. Exact source `13c097d` passed all five hosted jobs and 70/70 tests in each local Linux profile, with installation and staged execution. See [consolidation evidence](CONSOLIDATION_2026-11-08.md) for Windows GPU skips and remaining hardware limits. Hardware and Phase 1 acceptance remain open. Earlier per-slice results below describe their historical sources.

Owner reports an NVIDIA RTX 4070. Actual VRAM/driver and measurements are still needed; do not infer them from the model name. Current character progress includes fitting, profiles, bone-role mapping, two-bone geometry/pose operations and stationary vertical terrain alignment. Walking foot locks, orientation and body constraints remain open.

## Latest ten-step batch

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

Hardware evidence tooling in progress (`codex/hardware-evidence`, ADR 0060): versioned GPU inventory and offscreen assessment with optional fixture-bound provisional budgets. These helpers do not tick 01.09–01.12 without actual hardware results. Missing GPU inventory, presentation pacing and memory-residency evidence remain explicit.


## 2. Character and terrain slice

- [ ] **02.01** Define character import conventions and validation.
- [ ] **02.02** Add rig inspection, scale fitting and capsule alignment.
  - [ ] **02.02a** Visual bind-height inspection and explicit uniform height/feet-origin fitting — **implemented/in review**, [#59](https://github.com/Avrenthrad/lykowareGE/pull/59), source `1aab213`; Linux checks pass; merge and Windows gates remain open.
  - [ ] **02.02b** Authored profile and humanoid bone-role mapping/inspection — **implemented/in review**, #60/#61; unique names and leg ancestry validated. Limb lengths, bend planes and IK remain separate.
  - [ ] **02.02c** Orientation correction and capsule/camera alignment.
  - [ ] **02.02d** Retargeting and rig compatibility diagnostics.
- [ ] **02.03** Configure clips through authored data.
  - [ ] **02.03a** Model/idle/walk/visual-height JSON profiles — **implemented/in review**, [#60](https://github.com/Avrenthrad/lykowareGE/pull/60), source `c469c81`; merge/Windows gates open.
  - [ ] **02.03b** Extend authored configuration to mapped rig roles and remaining locomotion states.
- [ ] **02.04** Add idle, walk, run, jump, fall and landing states.
- [ ] **02.05** Blend transitions without snapping.
- [ ] **02.06** Handle turning, slopes, steps and uneven ground.
- [ ] **02.07** Decide root motion versus controller-driven movement.
- [ ] **02.08** Add reusable animation graphs or equivalent configuration.
- [ ] **02.09** Add animation events and attachment sockets.
- [ ] **02.10** Retarget between supported rigs.
- [ ] **02.11** Add foot placement and foot IK.
  - [ ] **02.11a** Two-bone position solver — **implemented/in review**, [#62](https://github.com/Avrenthrad/lykowareGE/pull/62), source `9fec633`; no stretch, reach/fallback flags and transactional failures.
  - [ ] **02.11b** Validate mapped rest-pose limb lengths/bend planes and apply solved joint rotations.
    - [ ] **02.11b-engine** Validated direct-chain pose operation — **implemented/in review**, #63 `fbf7468`; unit scales only, no helper chains.
    - [ ] **02.11b-integration** Bridge mapped roles, rest/bend calibration and actor-world/model coordinates.
    - [ ] **02.11b-runtime** Prepare validated chains/reusable scratch and measure runtime costs.
  - [ ] **02.11c** Terrain probes, foot planting, offsets and temporal smoothing. **Partial:** stationary probes/clearance/smoothing/resets implemented in #64; walking locks and orientation remain open.
  - [ ] **02.11d** Integrate pelvis/body constraints and verify varied terrain/character rigs.
- [ ] **02.12** Add ragdolls and animation/physics blending.
- [ ] **02.13** Add stumble, recovery and active ragdoll.
- [ ] **02.14** Refine first/third-person camera transitions.
- [ ] **02.15** Validate character feel on Windows hardware.
- [ ] **02.16** Complete character correctness and performance gates.

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
- [ ] **05.04** Scale navigation and avoidance.
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
- [ ] **07.02** Add import diagnostics and incremental rebuilds.
- [ ] **07.03** Complete flagship lighting, shadows, materials, transparency and post-processing.
- [ ] **07.04** Add visibility, batching and GPU resource management.
- [ ] **07.05** Add required terrain, foliage, water and environmental rendering.
- [ ] **07.06** Add UI, text, fonts, localisation and accessibility foundations.
- [ ] **07.07** Complete audio mixing, spatialisation, streaming and prioritisation.
- [ ] **07.08** Generate navigation and update it dynamically.
- [ ] **07.09** Complete jobs, threading, scheduling and resource budgets.
- [ ] **07.10** Add save migrations, recovery and corruption handling.
- [ ] **07.11** Complete input rebinding and controller support.
- [ ] **07.12** Complete logging, crash reports, profiling and diagnostic capture.
- [ ] **07.13** Add replay/determinism where required.
- [ ] **07.14** Maintain co-op boundaries; implement/test networking if committed to release scope.
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
