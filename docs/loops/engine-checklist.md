# Engine loop acceptance checklist

Owner: Codex. Current agreed milestone: the [next 15 Phase 2 entries](../NEXT_15_BATCH.md), selected from [MASTER_CHECKLIST](../MASTER_CHECKLIST.md), not the full engine/editor roadmap. Latest owner loop instructions received 8 November 2026: work isolated, maximum 20 implementation/review iterations; no further merges or pushes to main. Earlier #68–#70 merges predate that instruction. The 12 previously merged items remain completed; three runtime requirements are implemented and verified on #71 but remain unmerged under the latest owner restriction. The master checklist retains its merge acceptance convention; do not check unmerged rows there.

Authoritative architecture: [AGENTS](../../AGENTS.md), [CONTRIBUTING](../../CONTRIBUTING.md), [ENGINE_VISION](../ENGINE_VISION.md), [ARCHITECTURE](../ARCHITECTURE.md), [ROADMAP](../ROADMAP.md), [WORK_BOARD](../WORK_BOARD.md), ADRs 0062–0065. Preserve C++20/CMake, Vulkan/Jolt, Windows x64 primary and Linux secondary. Minimal CLI/sample only; editor remains later Track E.

| Requirement / source | Implementation status | Reproducible acceptance / evidence |
|---|---|---|
| 02.01 conventions/import validation | Verified/merged #69 | `character.assets`, `character.inspect_cli`; source 8201a26, five CI profiles, 76/76 each native profile |
| 02.02a bind fitting | Verified #59→#65 | character fitting/skin tests, rerun final full suite |
| 02.02b named humanoid roles | Verified #60/#61→#65 | rig/foot-placement tests, rerun final full suite |
| 02.02c visual/capsule/camera alignment | Verified/merged #69 | character.assets and sandbox.imported_player, actual yaw/capsule/eye assertions |
| 02.02d supported-rig diagnostics | Verified/merged #69 | character.assets and character.inspect_cli compatibility/refusal cases |
| 02.03a versioned profiles | Verified #60→#65 | profile round trip/refusal and actual imported playback |
| 02.03b six clip selectors/roles | Verified #61/#68 | animation.locomotion, sandbox.imported_player |
| 02.04-runtime six states | Verified/merged #68 | real Jolt adapter in animation.locomotion; final suite |
| 02.05-crossfade interruption | Verified/merged #68 | animation.crossfade, including new atomic retarget refusal |
| 02.06 turning/terrain | Verified/merged #70 | player.turning/player.course; source 0487221, 77/77 each native and all five CI profiles |
| 02.07 motion authority | Verified/merged #70 | ADR 0064, player.turning profile refusal and physics preservation |
| 02.08 reusable authored configuration | Verified on #71; merged `35ad909` | animation.authored (ordered/min-age edges, interruption, all six checked-in authored states, real controller integration); animation.replay_cli; actual sandbox.imported_player with profile graph |
| 02.09 events/sockets | Verified on #71; merged `35ad909` | animation.authored interval/wrap/non-loop/partition/capacity/posed socket tests; animation.replay_cli real glTF; sandbox evaluates final post-IK sockets |
| 02.10 supported retarget | Verified on #71; merged `35ad909` | animation.authored root basis/scale/target length/permuted indices/overlap/256-joint/refusals; animation.replay_cli source-to-target glTF and socket transfer |
| 02.11a analytical two-bone geometry | Verified #62→#65 | animation.two_bone_ik, animation.prepared_ik, final suite |

## Integration and final gates

- [x] Independent reviewer checks final implementation, callers, tests and limitations; findings resolved.
- [x] Clean build checkpoint and full applicable Linux Debug/sanitizers/clang-tidy suite from unchanged final source; configure/build/install/staged CLI evidence.
- [x] Hosted Windows Debug/Release and all three Linux profiles on exact final branch head. Missing actual Windows GPU stays UNVERIFIED.
- [x] Exercise real sample and CPU CLI, including imported authored playback, startup/shutdown, deterministic scene replay, resource lifetime and rendering regressions through full CTest. No rendering algorithm change is intended.
- [x] Review final diff, docs/links, saved commands/source fingerprint and pending integration state.
- [ ] Merge remaining branch into main — intentionally not performed: latest owner instruction prohibits merges. Master checklist rows remain pending integration.

## Explicitly deferred / out of scope

Actual RTX 4070 VRAM/driver/frame pacing/RAM/VRAM budgets, owner character-feel acceptance, production rigs/art, general A/T-pose/helper/scaled retargeting, root-motion-driven collision, full foot-heading locks/active ragdolls, game/editor/content-suite systems and public/Pax gates. These remain UNVERIFIED or future requirements; this loop does not close Phase 1 hardware or broad Phase 2 acceptance. Generic authored playback supports bounded unit-scale poses; lower-level clip sampling retains broader scale support. No feature is represented by a stub: current CLI/native/sample callers must exercise the real runtime.

Final runtime evidence: `bb47ca7`, CI run [42480515225](https://github.com/Avrenthrad/lykowareGE/actions/runs/42480515225) all five profiles; local clean build plus 79/79 each Debug/sanitizers/analysis, zero skips, install/staged CLI and unchanged fingerprint. Independent reviewer recheck: no blocking findings. Windows hosted graphics remain 21 skips; actual target hardware is unverified. Exact commands, review fixes, captures and final documentation-tip verification location are in [progress](engine-progress.md). Final milestone status: **PARTIAL — integration intentionally pending**, not an iteration-limit stop.

## Continued project work — stationary terrain constraints

Owner's subsequent instruction explicitly expands the active work beyond the completed runtime batch: continue overall development and save integration/manual matters for later. The previous instruction not to start future work was superseded. Preserve [deferred items](deferred.md), including no merges/main pushes. Base #71 `7197587`; current branch `codex/terrain-foot-constraints`, ADR0066.

- [x] Owned physics surface normals, preserving existing ray/camera callers and read-only behavior.
- [x] Prepared joint-axis alignment with bounded rotations, snapshot lifetime, overlap/failure and allocation tests.
- [x] Opt-in stationary slope-normal orientation through actual profile/sandbox callers.
- [x] Bounded, smoothed visual pelvis compensation with full bilateral transaction/reset behavior.
- [x] Real terrain/far-origin/rig/normal/smoothing/refusal and actual imported-render checks.
- [x] Independent review, clean integration build, full native matrix and exact-head hosted profiles.
- [x] Record verified branch evidence and the next actionable project requirement; leave integration/hardware parked.

## Resumed owner request — finish the four listed work points

Work resumes on #73 at the owner's instruction; #71 and #72 are already verified and remain unmerged. This run completes walking acceptance and reconciles #74's status snapshot; it does not lift the no-merge/main-push restriction or start ragdolls/new roadmap modules.

- [x] Compile saved WIP and add real static/kinematic transport, stale/world-lifetime, phase, numeric and allocation checks.
- [x] Test controller held/swing/reach-release/bilateral rollback and authored entry/wrap/reset/hidden lift-replant cycles.
- [x] Exercise actual offscreen moving player through bounded input replay, direct/lyko parity and GPU-versus-CPU held-foot capture.
- [x] Independent review, clean build and full exact-source native/hosted matrix.
- [x] Reconcile STATUS, WORK_BOARD, master checklist, WIP history and #74 snapshot with verified evidence.
- [x] Commit/push all completed work; keep integration/hardware explicitly pending.
