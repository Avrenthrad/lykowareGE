# Lykoware status

## Verified baseline

Original Module 01: C++20/CMake engine library, separate console sandbox and logging. [Baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f); [Windows Debug/Release and Linux CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
C++20/CMake is accepted by Joshua in ADR 0001.
Graphics accepted in ADR 0002: Vulkan 1.3 used directly from C++ behind a Lykoware-owned RHI. wgpu and ash are rejected. No graphics dependency is vendored.
Data formats accepted in ADR 0004: canonical JSON for machine-written data, TOML for hand-edited config, glTF 2.0 for meshes, binary only as cooked output. RON is dropped. No parser is vendored.
Jolt remains proposed in ADR 0003. If adopted, call it natively from C++ rather than through bindings.

## Current development task

Task owner: Cursor cloud agent. Branch feat/module02-window-input. Base: da71b1e9b9d08ead31f8c03e8ceb325deeca0cf4 on feat/phase0-image-harness ([PR #7](https://github.com/Avrenthrad/lykowareGE/pull/7), not merged). The pull request targets that branch until #7 merges, and it is not merged by this task.
Scope: vendored SDL 3.4.18 behind `lykoware::Window` and `lykoware::InputState`. Public headers do not include SDL. The wrapper creates and destroys a window, pumps events, and exposes keyboard, mouse, and one gamepad. Resize, DPI, and close are on the same object. A Vulkan surface hook takes an opaque `VkInstance` and reports instance extensions. Vulkan is not linked. ADR 0002 is accepted; this wrapper does not implement the RHI. ADR 0008 is Proposed, not accepted. Tests use the dummy video driver. `lykoware_sandbox --window` is optional. `lyko` is unchanged. No Phase 1 checkbox is ticked.

Local checks on this Linux machine (GCC 13.3.0):
- Debug and ASan/UBSan: `input.mapping`, `window.lifecycle`, `image.harness.api`, `image.harness.golden_pass`, `image.harness.golden_fail`, `trace.api`, `perf.sandbox_startup`, `perf.cpu_fixture`, `perf.budget_rejects_overrun`, `perf.budget_accepts_within`, `foundation.smoke`, `cli.commands`, and `cli.integration` passed. `foundation.smoke` includes `lykoware_sandbox --window --dummy --frames 1`.
- `window.lifecycle` ran with no display. It resized, closed, reopened, pumped a key, mouse, and virtual gamepad, and rejected a Vulkan surface call with a null instance.
- clang-tidy 18 could not run as the analysis preset on this VM: `/usr/bin/c++` is clang and that clang-tidy invocation did not see the GCC 13 libstdc++ headers. The same preset is the CI job. A manual clang-tidy 18 pass over the new translation units, given the GCC include path, reported no findings in those files.

CI: [run 41454076091](https://github.com/Avrenthrad/lykowareGE/actions/runs/41454076091) passed all five jobs: Windows Debug, Windows Release, Linux Debug, ASan/UBSan, and clang-tidy 18. Each job built and tested `input.mapping` and `window.lifecycle`. Tested code commit: 439917c. Delivery: [PR #8](https://github.com/Avrenthrad/lykowareGE/pull/8). Artifact upload did not run; it is opt-in and was not a failure. No storage was purchased. No checklist box was ticked. No paid operation.

## Previous image-harness task, still in review

Task owner: Cursor cloud agent. Branch feat/phase0-image-harness. Base: 9a2e9b5254011f2b4b6001fa4762dab364596918 on feat/phase0-cpu-trace-budget ([PR #6](https://github.com/Avrenthrad/lykowareGE/pull/6), not merged). The pull request targets that branch until #6 merges, and it is not merged by this task.
Scope: a GPU-free image harness inside `lykoware_engine` (no new library and no dependency). `Image`, `ImageCapture`, and `TestPatternCapture` produce a deterministic software pattern. Hand-written PPM P6 read/write, a per-pixel tolerance comparison, and a diff image live beside it. CTest uses small fixtures in `tests/golden/` and writes only under the build directory. `lyko screenshot` stays exit 3, including when given extra arguments. This is not a renderer screenshot test. Real Vulkan offscreen capture plugs into `ImageCapture` later. ADR 0002 is accepted; this harness does not implement it. No image/performance-gate checklist box is ticked.

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

ADR 0002 (Vulkan 1.3 from C++ behind an owned RHI) and ADR 0004 (canonical JSON, TOML config, glTF 2.0 meshes) are accepted. ADR 0003 physics stays proposed. That acceptance adds no renderer, parser, or graphics dependency. SDL 3.4.18 is vendored later, behind the window wrapper, and is not a graphics backend.

The supplied Lykoware logo vector set is under `assets/brand/`. Those files do not change the runtime, API, or schema. Artifact upload stays opt-in and is continue-on-error so a storage-quota rejection does not fail the foundation check.

## Next

1. Owner decision on ADR 0008. The wrapper is implemented and the ADR stays Proposed until then.
2. Phase 1 clocks and fixed-step update. Window and input do not include that clock.
3. Resolve native Jolt through ADR 0003 before adding physics. Do not vendor Vulkan-Headers, volk, Vulkan Memory Allocator, nlohmann/json or toml++ until a caller needs them; record exact licences and versions then. Review the shader-compiler licence as a build-time tool, and evaluate a software Vulkan driver such as Mesa lavapipe in CI, before claiming graphics CI.
4. Agree multiplayer intent, exact hardware profiles, world/simulation scope and budgets.

## Known issues and limitations

- No renderer, ECS, scene loader, physics, audio/animation runtime, world streaming or inference. A window and input snapshot exist. ADR 0008 is still Proposed.
- `lyko run` launches the console sandbox only. `run --scene` and screenshot print `unsupported: no renderer` and return exit 3. `lyko trace --frames N` writes a CPU Chrome trace of the sandbox loop. It is not a GPU capture.
- The image harness compares a software test pattern with committed PPM fixtures. It is not a screenshot, not a GPU capture, and not the image/performance gate. Vulkan offscreen capture is not implemented. ADR 0002 is accepted and is the later plug-in point.
- Provisional CPU budgets in CTest apply only to windows-2022 and ubuntu-24.04. They are not an RTX 4070 or MSI Claw gate, and no screenshot gate exists.
- Sanitizers are Linux-only; ThreadSanitizer is deferred until concurrent engine systems.
- The CLI is a developer process wrapper, not the future runtime/editor mutation command bus. See CLI.md for limitations.
- SDL 3.4.18 is the only vendored runtime. HIDAPI inside it is used under the BSD-style licence, not GPL. The Vulkan 1.3 RHI direction and data encodings are accepted decisions only; no renderer or parser is implemented. Jolt remains proposed in ADR 0003. wgpu, ash and RON are not open tasks. The surface hook does not link Vulkan and fails closed when no loader or instance is available. A Linux desktop window needs X11 or Wayland headers at configure time. Without them, the dummy driver still builds.
- Pax/publication gate remains policy; no Pax implementation or legal clearance.
- No paid service/spend or personal-data export was performed.
- GitHub artifact storage was exhausted; downloadable CI binaries remain unavailable until storage is available and upload is explicitly requested. Builds/tests do not depend on uploads.

## Handoff

- Task: window and input. Branch feat/module02-window-input. Base da71b1e on feat/phase0-image-harness (PR #7, not merged).
- Completed behavior: `Window` and `InputState` in the engine library. SDL 3.4.18 is vendored, static, and hidden. Dummy-driver create, resize, DPI, pump, close, and reopen. Keyboard, mouse, and one gamepad. Vulkan instance-extension query and surface create/destroy without linking Vulkan. `lykoware_sandbox --window` and `--window --dummy --frames N`. `lyko` unchanged.
- Files/interfaces: `engine/include/lykoware/window.hpp`, `engine/include/lykoware/input.hpp`, `engine/src/window.cpp`, `engine/src/input.cpp`, `engine/src/platform_map.cpp`, `third_party/sdl3/`, ADR 0008.
- Checks: local GCC 13.3.0 Debug and ASan/UBSan passed the full CTest set, including `input.mapping`, `window.lifecycle`, and `foundation.smoke`. [Run 41454076091](https://github.com/Avrenthrad/lykowareGE/actions/runs/41454076091) passed Windows Debug/Release and all three Linux profiles. Tested code commit 439917c. The local analysis preset did not run because this VM's default `c++` is clang; the CI clang-tidy job passed.
- Unverified: a real Win32 or X11 window on a desktop, focus changes, and a successful Vulkan surface. Downloadable CI artifacts were not uploaded.
- Next: record CI, then merge only with owner approval. Do not treat ADR 0008 as accepted. Do not tick Phase 1.
- Checklist: no box ticked.

Earlier image-harness handoff, for the branch this work builds on:

- Task: GPU-free image harness. Branch feat/phase0-image-harness. Base 9a2e9b5 on feat/phase0-cpu-trace-budget (PR #6, not merged).
- Completed behavior: `Image`, `ImageCapture`, and `TestPatternCapture` in the engine library; hand-written PPM P6 read/write; per-pixel tolerance comparison and diff image. CTest golden pass and golden fail. `lyko screenshot` still exits 3. Not a renderer screenshot.
- Files/interfaces: `engine/include/lykoware/image.hpp`, `engine/src/image.cpp`, `tests/image_harness.cpp`, `tests/golden/`, ADR 0007. ADR 0002 records the later Vulkan offscreen plug-in and is accepted; this harness does not implement it.
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
