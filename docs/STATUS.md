# Lykoware status

## Verified baseline

Original Module 01: C++20/CMake engine library, separate console sandbox and logging. [Baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f); [Windows Debug/Release and Linux CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
C++20/CMake is accepted by Joshua in ADR 0001.

## Current development task

Task owner: Cursor cloud agent. Branch: feat/phase0-lyko-cli. Base: ca59854d519b5da02f44d34507aa62b8803a626b.
Scope: one small lyko dispatcher in tools/cli. `build` and `test` still call the CMake/CTest presets. `run` launches lykoware_sandbox. `screenshot`, `trace`, and `run --scene` print `unsupported: no renderer` and return exit 3. No engine API, scene format, renderer, or command bus.

Local checks on this Linux machine (GCC 13.3.0). These are not Windows results:
- Debug preset: configure, build, and `ctest --preset linux-debug` passed `foundation.smoke`, `cli.commands`, and `cli.integration`.
- ASan/UBSan preset: configure, build, and `ctest --preset linux-sanitizers` passed the same three tests.
- Installed layout: staged `lyko run` launched the sibling sandbox even when the build-tree sandbox was removed, and preferred the sibling when both existed. `screenshot`, `trace`, and `run --scene` exited 3. With the sibling removed as well, `run` exited 127.

clang-tidy 18.1.3 reported no findings on tools/cli/commands.cpp, main.cpp, and process.cpp when given this machine's GCC 13 headers. The linux-analysis preset itself did not finish here: local clang-tidy selects missing GCC 14 libstdc++ headers. Windows Debug/Release were not run here. The pull request CI is the check for the full matrix. No checklist item was ticked for this change. No paid operation.

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
- `lyko run` launches the console sandbox only. `run --scene`, screenshot, and trace print `unsupported: no renderer` and return exit 3. No screenshot or perf-budget gate exists.
- Sanitizers are Linux-only; ThreadSanitizer is deferred until concurrent engine systems.
- The CLI is a developer process wrapper, not the future runtime/editor mutation command bus. See CLI.md for limitations.
- No dependencies are vendored. Vulkan/Jolt/data choices remain proposed.
- Pax/publication gate remains policy; no Pax implementation or legal clearance.
- No paid service/spend or personal-data export was performed.
- GitHub artifact storage was exhausted; downloadable CI binaries remain unavailable until storage is available and upload is explicitly requested. Builds/tests do not depend on uploads.

## Handoff

- Task: minimal lyko dispatcher. Branch feat/phase0-lyko-cli. Base ca59854d519b5da02f44d34507aa62b8803a626b. PR link belongs in this section after it is opened.
- Completed behavior: `build`/`test` unchanged; `run` launches lykoware_sandbox; renderer commands exit 3 with `unsupported: no renderer`.
- Files: tools/cli dispatcher and process-header contract, `cli.commands` ctest, CI staged checks, CLI/ADR/STATUS/WORK_BOARD notes.
- Checks: Linux Debug and ASan/UBSan ctest passed locally, plus the installed sibling-sandbox check above. clang-tidy on the three tools/cli sources reported no findings with an explicit GCC 13 include path. The analysis preset and Windows Debug/Release are not yet recorded.
- Unverified: Windows. No renderer, scene load, screenshot, or trace implementation.
- Next: record the PR CI result here, including a Windows artifact-upload failure if that is the only failure. Do not buy storage. Tick a checklist box only after that evidence exists.
- Checklist: no new box ticked.

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and [ADRs](adr/README.md). Check latest main and active PRs; do not implement from a stale chat. Update this file with exact commits/checks, failures and next actions.
