# Lykoware status

## Verified baseline

Original Module 01: C++20/CMake engine library, separate console sandbox and logging. [Baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f); [Windows Debug/Release and Linux CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
C++20/CMake is accepted by Joshua in ADR 0001.
Graphics accepted in ADR 0002: Vulkan 1.3 used directly from C++ behind a Lykoware-owned RHI. wgpu and ash are rejected. No graphics dependency is vendored.
Data formats accepted in ADR 0004: canonical JSON for machine-written data, TOML for hand-edited config, glTF 2.0 for meshes, binary only as cooked output. RON is dropped. No parser is vendored.
Jolt remains proposed in ADR 0003. If adopted, call it natively from C++ rather than through bindings.

## Current development task

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

This branch accepts ADR 0002 (Vulkan 1.3 from C++ behind an owned RHI) and ADR 0004 (canonical JSON, TOML config, glTF 2.0 meshes). ADR 0003 physics stays proposed. No renderer, parser, or vendored dependency is added.

## Next

1. Phase 1 window/input/timing, with a pinned vendored SDL3 evaluation and explicit lifecycle contract.
2. Resolve native Jolt through ADR 0003 before adding physics. Do not vendor Vulkan-Headers, volk, Vulkan Memory Allocator, nlohmann/json or toml++ until a caller needs them; record exact licences and versions then. Review the shader-compiler licence as a build-time tool, and evaluate a software Vulkan driver such as Mesa lavapipe in CI, before claiming graphics CI.
3. Agree multiplayer intent, exact hardware profiles, world/simulation scope and budgets.

## Known issues and limitations

- No window, renderer, ECS, scene loader, physics, audio/animation runtime, world streaming or inference.
- run, screenshot and trace return a clear unsupported error. No screenshot/perf-budget gate exists.
- Sanitizers are Linux-only; ThreadSanitizer is deferred until concurrent engine systems.
- The CLI is a developer process wrapper, not the future runtime/editor mutation command bus. See CLI.md for limitations.
- No dependencies are vendored. The Vulkan 1.3 RHI direction and data encodings are accepted decisions only; no renderer or parser is implemented. Jolt remains proposed in ADR 0003. wgpu, ash and RON are not open tasks.
- Pax/publication gate remains policy; no Pax implementation or legal clearance.
- No paid service/spend or personal-data export was performed.
- GitHub artifact storage was exhausted; downloadable CI binaries remain unavailable until storage is available and upload is explicitly requested. Builds/tests do not depend on uploads.

## Handoff

Read [WORK_BOARD.md](WORK_BOARD.md), [CLI.md](CLI.md), [ROADMAP.md](ROADMAP.md) and [ADRs](adr/README.md). Check latest main and active PRs; do not implement from a stale chat. Update this file with exact commits/checks, failures and next actions.
