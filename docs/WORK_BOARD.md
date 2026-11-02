# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).

Active task owner: Cursor cloud agent. Branch feat/module02-window-input. Base: da71b1e9b9d08ead31f8c03e8ceb325deeca0cf4 on feat/phase0-image-harness ([PR #7](https://github.com/Avrenthrad/lykowareGE/pull/7), not merged). Not merged.
Scope: vendored SDL 3.4.18 behind `lykoware::Window` and `lykoware::InputState`. No SDL types in public headers. Dummy-driver tests for input mapping and window lifecycle. Optional `lykoware_sandbox --window`. `lyko` unchanged. Vulkan surface hook without a Vulkan dependency. ADR 0008 is Proposed. No checklist box ticked.
Local GCC 13.3.0 Debug and ASan/UBSan passed the full CTest set, including `input.mapping`, `window.lifecycle`, and `lykoware_sandbox --window --dummy --frames 1`. Acceptance passed on [run 41454076091](https://github.com/Avrenthrad/lykowareGE/actions/runs/41454076091): Windows Debug/Release and Linux Debug, ASan/UBSan, and clang-tidy. Tested code commit 439917c. Delivery: [PR #8](https://github.com/Avrenthrad/lykowareGE/pull/8). Artifact upload stayed opt-in and did not fail.
Handoff: do not describe the window as a renderer, and do not treat ADR 0008 as accepted. Merge only with owner approval.

Review task owner: Cursor cloud agent. Branch feat/phase0-image-harness. Base: 9a2e9b5254011f2b4b6001fa4762dab364596918 on feat/phase0-cpu-trace-budget ([PR #6](https://github.com/Avrenthrad/lykowareGE/pull/6), not merged). Not merged.
Scope: GPU-free image harness in the existing engine library. `Image` and `ImageCapture`, a deterministic software pattern, hand-written PPM P6, and a per-pixel tolerance comparison with a diff image. Golden fixtures in `tests/golden/`. CTest writes only under the build directory. `lyko screenshot` remains exit 3. Contract: engine/include/lykoware/image.hpp and ADR 0007. Real Vulkan offscreen capture plugs into this harness later; ADR 0002 stays proposed. No new module, dependency, or renderer. The image/performance-gate checklist box stays open.
Acceptance passed on [run 41330369114](https://github.com/Avrenthrad/lykowareGE/actions/runs/41330369114): Windows Debug/Release and Linux Debug, ASan/UBSan, and clang-tidy, including `image.harness.golden_pass`, `image.harness.golden_fail`, and exit 3 for `screenshot`. Tested code commit fc3860e. Delivery: [PR #7](https://github.com/Avrenthrad/lykowareGE/pull/7). Local GCC 13.3.0 Debug, ASan/UBSan, and clang-tidy also passed. No checklist item ticked. The image/performance-gate box stays open. Artifact upload stayed opt-in and did not fail.
Handoff: do not describe the harness as a screenshot test, a scene capture, or a Vulkan backend. Merge only with owner approval.

Review task owner: Cursor cloud agent. Branch feat/phase0-cpu-trace-budget. Base: cb5f38c28be6d6d0a95d632b891406120219d459 on feat/phase0-lyko-cli ([PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), not merged). Not merged.
Scope: `CpuTimer` and `CpuTrace` in the existing engine library. `lyko trace --frames N` writes a hand-built Chrome trace of the sandbox CPU loop. `screenshot` and `run --scene` remain exit 3. Provisional CPU-only CTest thresholds are in the root `CMakeLists.txt` for windows-2022 and ubuntu-24.04. Contract: engine/include/lykoware/trace.hpp and ADR 0006. No new module, dependency, or budget file.
Acceptance passed on [run 41328924195](https://github.com/Avrenthrad/lykowareGE/actions/runs/41328924195): Windows Debug/Release and Linux Debug, ASan/UBSan, and clang-tidy, including staged `lyko trace --frames 2` and exit 3 for screenshot and `run --scene`. Tested code commit df728c0. Local GCC 13.3.0 Debug and ASan/UBSan ctest also passed. No checklist item ticked. The hardware-profile and image/performance-gate boxes stay open. Artifact upload stayed opt-in and did not fail.
Handoff: do not describe screenshot, scene load, or GPU traces as working, and do not describe the provisional budgets as a device gate. Merge only with owner approval.

Review task owner: Cursor cloud agent. Delivery: [PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), branch feat/phase0-lyko-cli. Base: ca59854d519b5da02f44d34507aa62b8803a626b. Not merged.
Scope at that revision: tools/cli dispatcher only. `lyko build` and `lyko test` call the existing presets. `lyko run` launches lykoware_sandbox. `screenshot`, `trace`, and `run --scene` were capability errors (exit 3). The CPU trace task above replaces the `trace` result with a CPU capture and leaves screenshot and `run --scene` unchanged. The image harness does not change `screenshot`.
Acceptance for that revision passed on [run 41327365270](https://github.com/Avrenthrad/lykowareGE/actions/runs/41327365270). Tested code commit 7b61fe8. No checklist item ticked.

Completed task owner: Codex. Delivery: [PR #3](https://github.com/Avrenthrad/lykowareGE/pull/3), branch feat/lyko-cli. Base: 850bd50451ecf108e353716fca24db5a685ddd00.
Scope: reconcile notes and implement native build/test CLI plus quality gates. Public engine APIs/formats unchanged; CLI contract is ADR 0005.
Acceptance passed: actual CMake/CTest invocation, argument/Unicode/cwd/error checks, Windows Debug/Release staged execution, Linux sanitizer/static analysis and clean docs. Evidence: [run 41325853657](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325853657), code commit 04243e6. No next implementation owner/branch assigned.
C++20/CMake is accepted in ADR 0001; graphics/physics/data choices remain open.
Module 01 console foundation remains verified; previous Module 02 is now part of Phase 1.
Owner-supplied vision is captured in [ENGINE_VISION.md](ENGINE_VISION.md).

Check latest main and active PRs before starting. Documents do not automatically synchronize assistants or reserve work.

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

## Suggested next task

Leave ADR 0008 Proposed until the owner accepts it. Next implementation is the Phase 1 clock and fixed step, then graphics, physics, and data ADRs before their dependent code. No full editor or content suite work before the roadmap gates.
