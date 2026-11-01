# Lykoware status

## Verified baseline

Original Module 01: C++20/CMake engine library, separate console sandbox and logging. [Baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f); [Windows Debug/Release and Linux CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
C++20/CMake is accepted by Joshua in ADR 0001.

## Current development task

Owner: Codex. Branch: feat/lyko-cli. Base: 850bd50451ecf108e353716fca24db5a685ddd00.
Scope: reconcile brief/notes; add lyko build/test and native quality gates. No game runtime/API/schema change.

Implemented in this branch:
- Native lyko build/test commands with no-shell child launch, explicit project/preset/tool paths and exit-code propagation.
- Integration tests against real CMake/CTest plus argument/cwd/failure fixtures, including Windows Unicode and quoting.
- Centralized warnings-as-errors; Linux ASan/UBSan and clang-tidy 18 presets/jobs.
- Installation/CI staged execution of lyko alongside the sandbox.
- Master brief reconciled with C++/native Vulkan/Jolt proposals, text/media distinctions, realistic perf targets, command sequencing and repository-only roadmap.
- CLI contract and ADR 0005.

Local validation: GCC 14.2 Debug build and two CTest suites passed; ASan/UBSan build/tests passed; clang-tidy 18.1.8 build/tests passed. The installed CLI successfully configured/built and tested this repository. Windows/CI results are recorded below when complete. These checks do not establish device performance or rendered output.

First CI run: [41325630051](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325630051) verified Windows Debug/Release compile/tests/staged execution and all three Linux profiles. Windows jobs then failed only at artifact upload because GitHub storage quota was exhausted. Uploads are now manual opt-in; validation stays mandatory. A follow-up run will verify that workflow change. No billing changes or artifact deletion.

## Next

1. Complete Windows Debug/Release and quality-matrix CI for this task; review/merge through its PR.
2. Phase 1 window/input/timing, with a pinned vendored SDL3 evaluation and explicit lifecycle contract.
3. Resolve graphics, native Jolt, and authored data ADRs before their dependent implementation.
4. Agree multiplayer intent, exact hardware profiles, world/simulation scope and budgets.

## Known issues and limitations

- No window, renderer, ECS, scene loader, physics, audio/animation runtime, world streaming or inference.
- run, screenshot and trace return a clear unsupported error. No screenshot/perf-budget gate exists.
- Sanitizers are Linux-only; ThreadSanitizer is deferred until concurrent engine systems.
- The CLI is a developer process wrapper, not the future runtime/editor mutation command bus. See CLI.md for limitations.
- No dependencies are vendored. Vulkan/Jolt/data choices remain proposed.
- Pax/publication gate remains policy; no Pax implementation or legal clearance.
- No paid service/spend or personal-data export was performed.
- GitHub artifact storage was exhausted; downloadable CI binaries remain unavailable until storage is available and upload is explicitly requested. Builds/tests do not depend on uploads.

## Handoff

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and [ADRs](adr/README.md). Check latest main and active PRs; do not implement from a stale chat. Update this file with exact commits/checks, failures and next actions.
