# Lykoware status

## Verified baseline

Original Module 01: C++20/CMake engine library, separate console sandbox and logging. [Baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f); [Windows Debug/Release and Linux CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
C++20/CMake is accepted by Joshua in ADR 0001.

## Current development task

Task owner: Cursor cloud agent. Branch feat/phase0-image-harness. Base: 9a2e9b5254011f2b4b6001fa4762dab364596918 on feat/phase0-cpu-trace-budget ([PR #6](https://github.com/Avrenthrad/lykowareGE/pull/6), not merged). The pull request targets that branch until #6 merges, and it is not merged by this task.
Scope: a GPU-free image harness inside `lykoware_engine` (no new library and no dependency). `Image`, `ImageCapture`, and `TestPatternCapture` produce a deterministic software pattern. Hand-written PPM P6 read/write, a per-pixel tolerance comparison, and a diff image live beside it. CTest uses small fixtures in `tests/golden/` and writes only under the build directory. `lyko screenshot` stays exit 3, including when given extra arguments. This is not a renderer screenshot test. Real Vulkan offscreen capture plugs into `ImageCapture` later; ADR 0002 stays proposed. No image/performance-gate checklist box is ticked.

Local checks on this Linux machine (GCC 13.3.0):
- Debug and ASan/UBSan: `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `trace.api`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `foundation.smoke`, `cli.commands`, and `cli.integration` passed.
- clang-tidy 18 analysis preset compiled the engine, sandbox, lyko, and the tests with warnings as errors, then passed the same CTest set.
- `lyko screenshot`, including `--harness` and `--output`, still prints `unsupported: no renderer` and returns exit 3.

CI: [run 41330369114](https://github.com/Avrenthrad/lykowareGE/actions/runs/41330369114) passed all five jobs: Windows Debug, Windows Release, Linux Debug, ASan/UBSan, and clang-tidy 18. Each job built and tested the harness, including the golden pass and golden fail, and staged `lyko screenshot` at exit 3. Tested code commit: fc3860eab0ce934e6700575b1e8d989f3eb35c61. Delivery: [PR #7](https://github.com/Avrenthrad/lykowareGE/pull/7). Artifact upload did not run; it is opt-in and was not a failure. No storage was purchased. No checklist box was ticked. No paid operation.

## Previous task, still in review

Task owner: Cursor cloud agent. Branch feat/phase0-cpu-trace-budget. Base: cb5f38c28be6d6d0a95d632b891406120219d459 on feat/phase0-lyko-cli ([PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), not merged). The pull request targets that branch until #5 merges, and it is not merged by this task.
Scope: CPU timer and scoped trace inside `lykoware_engine` (no new library). `lyko trace --frames N` runs the sandbox CPU loop and writes hand-built Chrome trace JSON. `screenshot` and `run --scene` stay exit 3. Provisional CPU-only CTest thresholds live in the root `CMakeLists.txt` for windows-2022 and ubuntu-24.04. No TOML/JSON budget file, no new dependency, and no hardware-profile or image/performance-gate checklist box is ticked.

Local checks on this Linux machine (GCC 13.3.0):
- Debug and ASan/UBSan: `trace.api`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `foundation.smoke`, `cli.commands`, and `cli.integration` passed.
- Measured against the 20000 ms provisional limits: sandbox startup about 1.1 ms; one million `cpu_fixture_iteration` steps about 323 ms.
- clang-tidy 18 analysis preset compiled the engine, sandbox, lyko, and the new tests with warnings as errors.

CI: [run 41328924195](https://github.com/Avrenthrad/lykowareGE/actions/runs/41328924195) passed all five jobs: Windows Debug, Windows Release, Linux Debug, ASan/UBSan, and clang-tidy 18. Each job built, tested, and staged `lyko trace --frames 2` plus the exit-3 renderer commands. Tested code commit: df728c0b0db7819ca2b2539dba619b1e8016f4dd. Artifact upload did not run; it is opt-in and was not a failure. No storage was purchased. No checklist box was ticked. No paid operation.

## Earlier dispatcher task, still in review

Task owner: Cursor cloud agent. Delivery: [PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), branch feat/phase0-lyko-cli. Base: ca59854d519b5da02f44d34507aa62b8803a626b. Status: in review, not merged.
Scope: one small lyko dispatcher in tools/cli. `build` and `test` still call the CMake/CTest presets. `run` launches lykoware_sandbox. At that revision, `screenshot`, `trace`, and `run --scene` printed `unsupported: no renderer` and returned exit 3. The CPU trace task replaces only the `trace` result.

Local checks on this Linux machine (GCC 13.3.0):
- Debug and ASan/UBSan presets: `foundation.smoke`, `cli.commands`, and `cli.integration` passed.
- Installed layout: staged `lyko run` launched the sibling sandbox, including when the build-tree copy was removed, and preferred the sibling when both existed. Renderer commands exited 3. With no sandbox present, `run` exited 127.

CI: [run 41327365270](https://github.com/Avrenthrad/lykowareGE/actions/runs/41327365270) passed all five jobs: Windows Debug, Windows Release, Linux Debug, ASan/UBSan, and clang-tidy 18. Each job built, tested, and staged `lyko run` plus the exit-3 renderer commands. Tested code commit: 7b61fe834528e924314e5fd6fac95e6ca03676bc. Artifact upload did not run; it is opt-in and was not a failure. No storage was purchased. No checklist box was ticked: the open Phase 0 boxes are still unmet, and the lyko build/test boxes were already checked. No paid operation.

## Completed CLI baseline

Completed task owner: Codex. Delivery: [PR #3](https://github.com/Avrenthrad/lykowareGE/pull/3), source branch feat/lyko-cli. Base: 850bd50451ecf108e353716fca24db5a685ddd00.
Scope: reconcile brief/notes; add lyko build/test and native quality gates. No game runtime/API/schema change.

Implemented in this branch:
- Native lyko build/test commands with no-shell child launch, explicit project/preset/tool paths and exit-code propagation.
- Integration tests against real CMake/CTest plus argument/cwd/failure fixtures, including Windows Unicode and quoting.
- Centralized warnings-as-errors; Linux ASan/UBSan and clang-tidy 18 presets/jobs.
- Installation/CI staged execution of lyko alongside the sandbox.
- Master brief reconciled with C++/native Vulkan/Jolt proposals, text/media distinctions, realistic perf targets, command sequencing and repository-only roadmap.
- CLI contract and ADR 0005.

Local validation: GCC 14.2 Debug build and two CTest suites passed; ASan/UBSan build/tests passed; clang-tidy 18.1.8 build/tests passed. The installed CLI successfully configured/built and tested this repository. Windows/CI results are recorded below. These checks do not establish device performance or rendered output.

First CI run: [41325630051](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325630051) verified Windows Debug/Release compile/tests/staged execution and all three Linux profiles. Windows jobs then failed only at artifact upload because GitHub storage quota was exhausted. Uploads are now manual opt-in; validation stays mandatory. [Follow-up run 41325853657](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325853657) passed all five profiles: Windows Debug/Release, Linux Debug, ASan/UBSan and clang-tidy analysis, including staged CLI execution. Tested code commit: 04243e61d4ba108360c9928c1fe1fbcc188183bc. Subsequent changes only record this evidence in documentation. No billing changes or artifact deletion.

## Next

1. Phase 1 window/input/timing, with a pinned vendored SDL3 evaluation and explicit lifecycle contract.
2. Resolve graphics, native Jolt, and authored data ADRs before their dependent implementation.
3. Agree multiplayer intent, exact hardware profiles, world/simulation scope and budgets.

## Known issues and limitations

- No window, renderer, ECS, scene loader, physics, audio/animation runtime, world streaming or inference.
- `lyko run` launches the console sandbox only. `run --scene` and screenshot print `unsupported: no renderer` and return exit 3. `lyko trace --frames N` writes a CPU Chrome trace of the sandbox loop. It is not a GPU capture.
- The image harness compares a software test pattern with committed PPM fixtures. It is not a screenshot, not a GPU capture, and not the image/performance gate. Vulkan offscreen capture is not implemented; ADR 0002 remains proposed and is the later plug-in point.
- Provisional CPU budgets in CTest apply only to windows-2022 and ubuntu-24.04. They are not an RTX 4070 or MSI Claw gate, and no screenshot gate exists.
- Sanitizers are Linux-only; ThreadSanitizer is deferred until concurrent engine systems.
- The CLI is a developer process wrapper, not the future runtime/editor mutation command bus. See CLI.md for limitations.
- No dependencies are vendored. Vulkan/Jolt/data choices remain proposed.
- Pax/publication gate remains policy; no Pax implementation or legal clearance.
- No paid service/spend or personal-data export was performed.
- GitHub artifact storage was exhausted; downloadable CI binaries remain unavailable until storage is available and upload is explicitly requested. Builds/tests do not depend on uploads.

## Handoff

- Task: GPU-free image harness. Branch feat/phase0-image-harness. Base 9a2e9b5 on feat/phase0-cpu-trace-budget (PR #6, not merged).
- Completed behavior: `Image`, `ImageCapture`, and `TestPatternCapture` in the engine library; hand-written PPM P6 read/write; per-pixel tolerance comparison and diff image. CTest golden pass and golden fail. `lyko screenshot` still exits 3. Not a renderer screenshot.
- Files/interfaces: `engine/include/lykoware/image.hpp`, `engine/src/image.cpp`, `tests/image_harness.cpp`, `tests/golden/`, ADR 0007. ADR 0002 records the later Vulkan offscreen plug-in and stays proposed.
- Checks: local GCC 13.3.0 Debug, ASan/UBSan, and clang-tidy 18 analysis passed. [Run 41330369114](https://github.com/Avrenthrad/lykowareGE/actions/runs/41330369114) passed Windows Debug/Release and all three Linux profiles. Tested code commit fc3860e. `screenshot` stayed exit 3.
- Unverified: any GPU or window capture. The harness extent guard is not a display mode. Downloadable CI artifacts were not uploaded.
- Next: merge only with owner approval. Do not describe the harness as a screenshot test.
- Checklist: no box ticked. The image/performance-gate item stays open.

Earlier CPU-trace handoff, for the branch this work builds on:

- Task: CPU trace and provisional hosted CPU budgets. Branch feat/phase0-cpu-trace-budget. Base cb5f38c on feat/phase0-lyko-cli (PR #5, not merged).
- Completed behavior: `CpuTimer` / `CpuTrace` in the engine library; `lyko trace --frames N` writes a Chrome trace of the sandbox CPU loop; screenshot and `run --scene` still exit 3; CTest fails when the provisional startup or fixture budget is exceeded.
- Files/interfaces: `engine/include/lykoware/trace.hpp`, `engine/src/trace.cpp`, sandbox `--frames`/`--trace`, lyko `trace`, root `CMakeLists.txt` thresholds, ADR 0006.
- Checks: local GCC 13.3.0 Debug, ASan/UBSan, and clang-tidy 18 analysis passed. [Run 41328924195](https://github.com/Avrenthrad/lykowareGE/actions/runs/41328924195) passed Windows Debug/Release and all three Linux profiles, including staged `lyko trace --frames 2`. Tested code commit df728c0.
- Unverified: GPU timing and device profiles. Downloadable CI artifacts were not uploaded. The provisional budgets are not a hardware profile.
- Next: record the CI run, then merge only with owner approval. Do not treat the provisional budgets as a hardware profile.
- Checklist: no box ticked. The hardware-profile and image/performance-gate items stay open.

Earlier dispatcher handoff, for the branch this work builds on:

- Task: minimal lyko dispatcher. Branch feat/phase0-lyko-cli. PR #5. Base ca59854d519b5da02f44d34507aa62b8803a626b. Tested commit 7b61fe8.
- Completed behavior at that revision: `build`/`test` unchanged; `run` launches lykoware_sandbox; renderer commands, including trace, exited 3. The current task changes the trace result only.
- Checks: [run 41327365270](https://github.com/Avrenthrad/lykowareGE/actions/runs/41327365270) passed Windows Debug/Release and all three Linux profiles for that revision.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and [ADRs](adr/README.md). Check latest main and active PRs; do not implement from a stale chat. Update this file with exact commits/checks, failures and next actions.
