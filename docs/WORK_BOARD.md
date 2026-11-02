# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).

Active task owner: Cursor cloud agent. Branch feat/phase0-cpu-trace-budget. Base: cb5f38c28be6d6d0a95d632b891406120219d459 on feat/phase0-lyko-cli ([PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), not merged). Not merged.
Scope: `CpuTimer` and `CpuTrace` in the existing engine library. `lyko trace --frames N` writes a hand-built Chrome trace of the sandbox CPU loop. `screenshot` and `run --scene` remain exit 3. Provisional CPU-only CTest thresholds are in the root `CMakeLists.txt` for windows-2022 and ubuntu-24.04. Contract: engine/include/lykoware/trace.hpp and ADR 0006. No new module, dependency, or budget file.
Acceptance passed on [run 41328924195](https://github.com/Avrenthrad/lykowareGE/actions/runs/41328924195): Windows Debug/Release and Linux Debug, ASan/UBSan, and clang-tidy, including staged `lyko trace --frames 2` and exit 3 for screenshot and `run --scene`. Tested code commit df728c0. Local GCC 13.3.0 Debug and ASan/UBSan ctest also passed. No checklist item ticked. The hardware-profile and image/performance-gate boxes stay open. Artifact upload stayed opt-in and did not fail.
Handoff: do not describe screenshot, scene load, or GPU traces as working, and do not describe the provisional budgets as a device gate. Merge only with owner approval.

Review task owner: Cursor cloud agent. Delivery: [PR #5](https://github.com/Avrenthrad/lykowareGE/pull/5), branch feat/phase0-lyko-cli. Base: ca59854d519b5da02f44d34507aa62b8803a626b. Not merged.
Scope at that revision: tools/cli dispatcher only. `lyko build` and `lyko test` call the existing presets. `lyko run` launches lykoware_sandbox. `screenshot`, `trace`, and `run --scene` were capability errors (exit 3). The active task above replaces the `trace` result with a CPU capture and leaves screenshot and `run --scene` unchanged.
Acceptance for that revision passed on [run 41327365270](https://github.com/Avrenthrad/lykowareGE/actions/runs/41327365270). Tested code commit 7b61fe8. No checklist item ticked.

Completed task owner: Codex. Delivery: [PR #3](https://github.com/Avrenthrad/lykowareGE/pull/3), branch feat/lyko-cli. Base: 850bd50451ecf108e353716fca24db5a685ddd00.
Scope: reconcile notes and implement native build/test CLI plus quality gates. Public engine APIs/formats unchanged; CLI contract is ADR 0005.
Acceptance passed: actual CMake/CTest invocation, argument/Unicode/cwd/error checks, Windows Debug/Release staged execution, Linux sanitizer/static analysis and clean docs. Evidence: [run 41325853657](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325853657), code commit 04243e6. No next implementation owner/branch assigned.
C++20/CMake (ADR 0001), Vulkan 1.3 behind an owned RHI (ADR 0002) and canonical JSON / TOML / glTF 2.0 (ADR 0004) are accepted. Physics remains proposed in ADR 0003. The graphics and data-format acceptance is recorded below.
Module 01 console foundation remains verified; previous Module 02 is now part of Phase 1.
Owner-supplied vision is captured in [ENGINE_VISION.md](ENGINE_VISION.md).

Check latest main and active PRs before starting. Documents do not automatically synchronize assistants or reserve work.

## Active task

Brand logo vector set — owner: cloud agent; status: review; base commit `850bd50` (`main`); branch `feat/brand-logo-vector-set`.

- Objective: add the supplied Lykoware logo vector set under `assets/brand/` unchanged and list that path in the build structure.
- Scope and affected paths: `assets/brand/`, `docs/BUILDING.md`, `docs/WORK_BOARD.md`, `docs/STATUS.md`.
- Shared interfaces/formats affected: none.
- Related tasks/PRs and dependencies: none. Does not replace the Phase 0 tooling task.
- Acceptance checks: brand files match the supplied set; `docs/BUILDING.md` names `assets/brand/`; foundation CI stays green.
- Assumptions and decisions needed: generator scripts and the concept image stay outside the repo, as `assets/brand/BRAND.md` states.

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

## Recorded task: accept ADR 0002 and ADR 0004

- Task/module: Phase 0 documentation. Accept the graphics and data-format ADRs and correct the brief.
- Owner and status: review. Documentation is written on `docs/accept-adr-0002-0004` and is not merged. No runtime owner.
- Base commit and branch: `850bd50451ecf108e353716fca24db5a685ddd00` on `main`; branch `docs/accept-adr-0002-0004`.
- Objective: Record Vulkan 1.3 behind a Lykoware-owned RHI, and canonical JSON / TOML / glTF 2.0, and make the brief agree.
- Scope and affected paths: documentation only. ADRs 0002 and 0004, the ADR index, the brief, STATUS, this board, and the planning pages that still described the old proposals.
- Shared interfaces/formats affected: graphics API direction and authored-data encodings. No schema, renderer, parser or vendored source.
- Related tasks/PRs and dependencies: ADR 0001 is accepted. ADR 0003 stays proposed. wgpu, ash and RON are rejected as Rust-oriented.
- Acceptance checks: the brief, ADR index, STATUS and this board name the same decisions; no code, dependency or spend; Windows and Linux CI still build the existing foundation.
- Assumptions and decisions needed: shader-compiler licence review and a software Vulkan driver such as Mesa lavapipe in CI are follow-ups, not accepted requirements. D3D12 only if Intel Arc measurements justify it. Parser libraries are chosen when first needed. Jolt is not selected by the brief's native-C++ wording.

### Handoff

- Task, branch, commit, PR: accept ADR 0002 and ADR 0004; branch `docs/accept-adr-0002-0004`; decision commit `0d9100ff73271a3828eaae55951a2f43a203d112`; pull request https://github.com/Avrenthrad/lykowareGE/pull/4.
- Completed behavior: ADR 0002 and ADR 0004 are Accepted on this branch. The brief no longer evaluates wgpu against ash, no longer puts Jolt behind bindings, and no longer lists RON as a format.
- Files/interfaces changed: documentation listed in the task. No public API or on-disk schema was implemented.
- Checks run and actual results: relative links in the edited Markdown resolve. Local Linux `cmake --preset linux-debug`, `cmake --build --preset linux-debug`, and `ctest --preset linux-debug` passed (`foundation.smoke`). Pull-request CI [run 41325777090](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325777090) on `d3c4e4535f698b31b7acdd212cabbe9b69d98ca3`: Linux portability check passed. Windows Debug and Release configure, build, test, stage, and staged executable passed. Both Windows jobs then failed on `actions/upload-artifact` because the account artifact storage quota is exhausted. No storage was purchased. That quota is not freed immediately by deleting artifacts.
- Unverified behavior/platforms: no renderer, capture path, parser round-trip or vendored library was exercised. The Windows failure is the artifact upload, not the build or test.
- Open decisions and blockers: ADR 0003 physics; multiplayer and hardware profiles; shader-compiler licence; lavapipe (or another software Vulkan driver) as a CI follow-up; exact parser choice at first use.
- Next concrete action: Phase 1 window/input/timing. Do not vendor graphics or parser dependencies until a caller needs them. ADR 0003 stays open.
- Checklist/documentation updates: graphics and data-format acceptance is checked. Physics acceptance stays open. STATUS next steps match this handoff.

## Suggested next task

Develop Phase 1 window/input/timing. Resolve ADR 0003 before adding physics. Obtain multiplayer and hardware answers while preparing representative tests. When rendering work starts, vendor pinned Vulkan-Headers, volk and Vulkan Memory Allocator under third_party and review the shader-compiler licence first. No full editor or content suite work before the roadmap gates.
