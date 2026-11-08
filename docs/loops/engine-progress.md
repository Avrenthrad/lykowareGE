# Engine loop progress

## Resume point — 8 November 2026

Branch `codex/authored-animation-runtime`, based on merged #70 `9555fea`; implementation checkpoint `9e3eb1d`, subsequent integration/tests/docs are in the working tree. Scope and acceptance: [engine-checklist](engine-checklist.md). Latest owner instructions prohibit further merges or pushes to main; task-branch push/PR may supply hosted verification. No paid services or release operations.

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
