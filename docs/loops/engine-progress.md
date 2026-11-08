# Engine loop progress

## Active continuation — terrain foot constraints

Owner explicitly expanded scope to continue the overall project and park matters needing later attention. Previous 15-item runtime batch remains verified on #71 and unmerged. Active branch `codex/terrain-foot-constraints`, base #71 `7197587`, ADR0066. Current task: read-only physics surface normals, prepared ankle-axis alignment, opt-in stationary slope orientation and bounded pelvis compensation, actual profile/sample integration and meaningful terrain tests. [Deferred ledger](deferred.md) preserves integrations/manual gates; no merge or main push.

Iteration4 starts by reproducing missing normal/orientation/pelvis behavior (current RayHit has no normal; controller only corrects vertical foot height). Implement minimum complete APIs/callers, then focused/native/hosted checks and independent review. Prior loop used3/20 iterations; continue within the remaining allowance. No need to repeat already-passed #71 tests unless affected.


## Iteration 1 — establish current milestone and reproduce missing integrations

The ongoing owner request is the existing next-15 batch: 12 entries merged, three authored-runtime entries remaining. Read current instructions/architecture/roadmap/work board; no competing implementation PR was open when this work started (#22/#30 are unrelated/already incorporated). Missing behavior: no generic authored state player, event/socket replay or supported-rig pose transfer on main. Added ADR 0065 before dependent implementation; new API, engine implementations, sandbox/profile consumers, CPU CLI and original fixtures exist on this branch. Source output protection now includes referenced graph files.

Focused commands (repository root; CMake 3.31.6/GCC14.2):

```sh
export PATH=/workspace/.build-tools/cmake/data/bin:/workspace/.build-tools/bin:$PATH
cmake --preset linux-debug
cmake --build --preset linux-debug -j2
LD_LIBRARY_PATH=/workspace/phase1-system/mesa/usr/lib/x86_64-linux-gnu \
VK_DRIVER_FILES=/workspace/phase1-system/mesa/usr/share/vulkan/icd.d/lvp_icd.json \
ctest --preset linux-debug -R 'animation.authored|animation.replay_cli|sandbox.imported_player|character.inspect_cli|animation.crossfade' --output-on-failure
```

Observed before this loop checkpoint: 5/5 focused checks pass, including actual offscreen Vulkan imported playback. Build `/workspace/authored-build.log`; tests `/workspace/authored-tests.log`. Authored graph visits all six example states; 1,000 graph/retarget updates allocate zero ordinary C++ storage after setup (positive allocation control included). Full exact-tree verification and independent review are pending; do not treat focused checks as completion. An initial test referenced a nonexistent after_step API; corrected to the actual before_step→World::step contract. No product checks weakened.

Changed paths: engine animation_graph/retarget headers and sources, atomic PoseCrossfade interruption, character profile/inspection, sandbox/course Playback, CLI animation command and source-output guard, CMake registration, authored/replay/imported tests, example graph, ADR/format/build/status/checklist docs.

Next action: independent review of branch diff against origin/main; resolve findings, run clean/native full matrix and exact-head hosted CI, record results, leave integration pending per owner restriction. Iteration count: 1/20; no repeated blocker attempts yet.

## Iteration 2 — independent review and analysis finding

Independent reviewer `review_animation_milestone` performed read-only static review of `5688eda` against `9555fea`. Blocking P2 finding: initialized sample Playback bound parameter names/count from replaceable external `asset.graph`; replacing it with >32 parameters could exceed fixed scratch before validation. Fixed binding to `graph_player->graph()` (owned, validated snapshot); added a 33-parameter replacement test and reset/revalidation refusal. Other core graph/retarget/CLI integration and test scope were judged consistent; review was static, not execution evidence.

Hosted clang-tidy run `42480155373` separately rejected a floating event-cycle loop (`clang-analyzer-security.FloatLoopCounter`). Replaced it with bounded uint64 iteration and an exact-cycle quotient limit (2^52), preserving failure-before-commit. This also addresses the reviewer's nonblocking eventual double-increment concern. No suppression or check weakening.

The clean Debug build of `5688eda` passed (554 native build steps). Local v1 validation passed Debug tests/install/staged CLI, then was explicitly interrupted while building sanitizers after the independent/CI findings made that source obsolete. `/workspace/lykoware-authored-animation-validation-v1/report.json` is INCOMPLETE, never a full-matrix pass. Do not reuse partial sanitizer objects as evidence; the next validator rebuilds changed dependencies. Final source needs clean build and all applicable profiles again.

Next action: finish focused regression and reviewer recheck, then clean rebuild/full matrix on the corrected commit, push task branch only for exact-head hosted checks. Iterations 2/20; one analysis failure, one corrective approach; no blocker has reached three failed attempts. Main stays untouched.

## Iteration 3 — final runtime acceptance and handoff

Independent reviewer rechecked corrected `bb47ca7` and found no blocking findings. The reviewer's work was static/read-only; execution evidence below is from the native validator and hosted CI. No independent-review availability limitation applies.

- Clean Debug rebuild: `cmake --build --preset linux-debug --clean-first -j2`, all 554 steps passed; `/workspace/authored-final-clean-build.log`.
- Full unchanged-source matrix: source `bb47ca7`, fingerprint `6b4ea9d75becedf0d4665c967b413f9194ee68d7fc01c6bbac9ab85921466f21`, report `/workspace/lykoware-authored-animation-final-validation/report.json`. Debug, ASan/UBSan, clang-tidy18 each 79 passed, zero failed/skipped, configure/build/discovery/test/install/staged CLI passed.
- Hosted exact-head [run42480515225](https://github.com/Avrenthrad/lykowareGE/actions/runs/42480515225): all five profiles passed (Windows Debug/Release plus three Linux). Windows GPU-dependent tests remain 21 skips; that is not RTX4070 verification.
- Full suites exercise actual imported authored sample startup/render/shutdown, CPU replay, scene persistence/replay, input/lifecycle/resource checks and existing graphics references. No renderer algorithm/shader change is included.
- Diagnostic authored graph capture: built an external copy of `tests/render_skinning.cpp` with the original procedural asset bound to `samples/course/locomotion.graph.json`; real GraphPlayer → skin palette → Vulkan output matched CPU-deformed pixels (existing tolerance2), exit0, validation errors0. Visually inspected `/workspace/authored-animation-capture/mannequin.ppm` (idle left, walking right; intact silhouettes, no exploding transforms). PPM SHA256 `3823b3e60991ddc0d8631d6e9a781f58a347bba34f2f3a5331a9282b926a030a`.
- Harness `/workspace/capture-authored-animation.py` SHA256 `ad484b3314f748c8b082e3ab8b9f81dbe17ca94494b548cd24287b2866f6e14e`; outputs outside Git. No fixture output is substituted for engine behavior.

Exact matrix command (same PATH/driver variables as iteration1):

```sh
python3 tools/validation/verify.py --preset linux-debug --preset linux-sanitizers --preset linux-analysis --clang-tidy /workspace/.analysis-tools/clang_tidy/data/bin/clang-tidy --output /workspace/lykoware-authored-animation-final-validation --jobs 2
python3 /workspace/capture-authored-animation.py
LD_LIBRARY_PATH=/workspace/phase1-system/mesa/usr/lib/x86_64-linux-gnu VK_DRIVER_FILES=/workspace/phase1-system/mesa/usr/share/vulkan/icd.d/lvp_icd.json /workspace/authored-capture build/linux-debug/shaders/lit.vert.spv build/linux-debug/shaders/lit.frag.spv /workspace/authored-animation-capture build/linux-debug/shaders/skinned.vert.spv
```

Fresh output directories are required for reruns. After recording this evidence, the documentation-only handoff tip is validated with the same three-preset command and fresh output `/workspace/lykoware-animation-handoff-validation`; check its report for exact final commit/fingerprint and the latest #71 five-profile CI. `git diff bb47ca7 HEAD -- engine samples tests tools CMakeLists.txt` must be empty: the clean verified runtime is unchanged. Markdown links and final diff are reviewed, with no generated/third-party asset output committed.

Iterations used: **3/20**. One hosted analysis finding was corrected, no repeated blocker exceeded the three-attempt limit. All actionable runtime work in the agreed batch is complete and verified. The remaining milestone gate is deliberately pending integration because the owner expressly prohibited merges; no permission request is needed to respect that instruction. Deferred GPU/VRAM/frame pacing/owner feel remain unverified and are not checked off.

## Iteration 4 — stationary surface orientation and pelvis constraints

Added owned RayHit normal data using the actual Jolt hit subshape and a read lock; camera/other ray callers retain existing fields. Added prepared, overlap-safe calibrated joint-axis alignment with bounded shortest-arc correction and reusable scratch. Real profile/sandbox paths opt into slope-normal smoothing/rejection and a leg-length/authored-bound visual pelvis drop; bilateral output/persistent state remains transactional, with no simulation writes.

Regression discovery: exact antiparallel alignment exposed an existing `between` helper that assigned float `hypot` results to double despite its double-normalization comment. Explicit double inputs fix the lost direction precision; both reference/prepared two-bone suites and the new antiparallel regression pass. A first heightfield assertion compared against uncompressed sample slopes at too-tight tolerance. Replaced that expectation with an independent geometric cross-product of neighboring real ray hit points on the compressed triangle; retained nominal-slope stance tolerance. No checks were removed/weakened to hide behavior.

Focused Debug checks: 8/8 pass (joint-axis, terrain stance, existing geometry/prepared/placement, far-origin physics, CPU inspection and actual imported Vulkan playback). `render.gpu_skinning` passes separately, including the original stance mesh, actual Jolt slope/controller and GPU-vs-CPU pixel reference. Visually inspected `build/linux-debug/gpu-skinning/terrain-stance.ppm`: rest left, corrected feet/pelvis right; no exploding geometry. Both new fixtures record 1,000 successful updates with zero ordinary C++ allocations after setup and positive allocation controls. Heightfield/slope/missing/steep contacts, parent-local pelvis transforms, far-origin/yaw/scale, drop bounds/resets and bilateral refusal are covered.

Commands: `cmake --preset linux-debug`; `cmake --build --preset linux-debug -j2`; with recorded Mesa driver variables, `ctest --preset linux-debug -R 'animation.joint_axis|animation.terrain_stance|animation.foot_placement|animation.two_bone_ik|animation.prepared_ik|physics.far_from_origin|sandbox.imported_player|character.inspect_cli' --output-on-failure`, then `ctest --preset linux-debug -R '^render.gpu_skinning$' --output-on-failure`. Logs `/workspace/terrain-stance-build.log`, `terrain-stance-focused.log`, `terrain-stance-render.log`.

Current source needs independent review, clean integration build and all native/hosted profiles before verification is recorded. Remaining overall work is still actionable; continue to walking planting/locks after this module is verified, leaving integrations/manual gates in deferred.md. Iteration count4/20.

## Iteration 5 — independent FK review correction

Reviewer found one P2 issue on `127e836`: prepared ankle verification used normalized quaternion accumulation while rendering uses raw pose FK matrices. A parent quaternion with norm1.0009 passes the existing near-unit tolerance but can cause rendered-axis error0.00127269, greater than the1e-4 verification tolerance. Corrected the final check to normalize the actual global matrix applied to the calibrated local axis, and added a near-unit-parent refusal/unchanged-output regression plus normalized-parent success. This preserves all caller outputs/diagnostics on inconsistency. Independent static review found no other blockers in pelvis/bilateral/normal/profile/sample integration. Added inside-solid ray-hit regression to protect the camera/query baseline.

Corrections were staged in a separate worktree while the initial unchanged-source clean build/matrix ran; do not claim initial127e836 evidence for corrected source. Focused/full native and hosted checks on the corrected commit remain required. Iteration5/20.

## Owner-requested pause and checkpoint

Development paused on explicit owner instruction to commit outstanding work, establish a solid status and return the primary checkout to main. Walking anchors were being drafted in a separate worktree while #72 validation completed. This branch saves that partial source as WIP; see [walking-foot-anchors-wip](walking-foot-anchors-wip.md). No walking build/test/review result exists. Do not continue automatically from this checkpoint until the owner evaluates next steps.
