# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).

`main` at b5c75ed contains PRs #2, #4–#21 and #23–#25. Details and CI evidence: [STATUS.md](STATUS.md). Earlier per-task entries on this board were removed once merged; they remain in the pull requests and git history.
Owner-supplied vision is captured in [ENGINE_VISION.md](ENGINE_VISION.md).

Owner rules for every task: small stacked PRs; commit small and often; keep all five CI jobs green; record each decision as an ADR; keep STATUS handoff-ready after every task; Joshua merges; no regenerable generated files in commits; artifact upload stays opt-in and no storage or service is bought; no roadmap box ticked unless truly done.

Check latest main and open PRs before starting. Documents do not automatically synchronize assistants or reserve work.

## Active task

Double-precision positions (ADR 0021 decided; ADR 0022 to record the build). Owner: Claude. Status: starting after the ADR 0021 docs PR.

- Objective: physics positions accurate to well under a millimetre anywhere in a 16 km+ world, still bit-identical across MSVC, GCC and Clang.
- Planned PRs, in order:
  1. Build Jolt with `DOUBLE_PRECISION` on; convert inside `physics.cpp`; the public API is unchanged. New determinism hashes must match in all five jobs; the benchmark is re-run.
  2. Public positions become `double` (bodies, characters, ray origins and hit points); a test shows bodies and characters 16 km from the origin behave like ones at the origin.
- Shared interfaces/formats affected: PR 2 changes `lykoware::physics` position types.
- Acceptance checks: all five CI jobs green; the same hashes in every job; the benchmark stays within its limit.
- Assumptions and decisions needed: ADR 0022 starts Proposed; the owner accepted the direction in ADR 0021.

## Completed task: physics spike

Owner: Claude. Done: #21, #23, #24 and #25 merged. Jolt v5.6.0 vendored with cross-platform determinism (ADR 0018); the `lykoware::physics` API (ADR 0020, accepted by the owner on 2026-11-04) with bodies, height-field terrain and walking characters; a body-count benchmark that also found and fixed a step-memory abort. Details and CI runs are in [STATUS.md](STATUS.md).

## Completed task: physics decision (ADR 0003)

Owner: Claude for the review; the owner decided on 2026-11-04 (#20). Jolt v5.6.0; the *Numerical Recipes*-derived functions are replaced by our own; cross-platform determinism on; floats for now.

## Completed task: Phase 1 clocks and fixed-step update

Owner: Claude. Done: #19 merged (ADR 0017, Proposed). `FixedStep` gives numbered fixed ticks (60 Hz default) with no drift and identical results at any frame rate; `FrameClock` measures real frame time; the sandbox animates per tick. The owner has not accepted ADR 0017 or its 60 Hz default.

## Completed task: Vulkan RHI bring-up

Owner: Claude. Done: PRs #11–#14 and #16–#18 merged, with ADR 0013 in #15. First pixels through a Lykoware-owned Vulkan 1.3 RHI (ADR 0002): instance, device, offscreen render target read back into the image harness, swapchain on the SDL window, Slang shaders compiled and validated at build time, graphics pipelines, and the triangle drawn offscreen (pixel-checked) and in the sandbox window. GPU tests run on Mesa lavapipe in Linux CI and report Skipped on Windows runners, which have no Vulkan. The owner has not accepted ADRs 0009–0012 or 0014–0016; merging them did not accept them. Details and CI runs are in [STATUS.md](STATUS.md) and each pull request.

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
- Owner and status: done. The content reached `main` through the merged #5–#8 stack; [PR #4](https://github.com/Avrenthrad/lykowareGE/pull/4) itself was closed. No runtime owner.
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
- Next concrete action (at the time): Phase 1 window/input/timing. Superseded by the owner order under "Suggested next task". Graphics dependencies are vendored with their first caller in the Vulkan RHI task. ADR 0003 stays open.
- Checklist/documentation updates: graphics and data-format acceptance is checked. Physics acceptance stays open. STATUS next steps match this handoff.

## Suggested next task

Owner order, set 2026-11-02: (1) Vulkan RHI bring-up, now done; (2) Phase 1 clocks and fixed-step update, now done; (3) decide Jolt in ADR 0003 before any physics, now decided, with the spike as the active task above; (4) agree hardware profiles, multiplayer intent, world scope and budgets, now decided in ADR 0021 (desktop only, RTX 4070 recommended at 1440p60, 16 GB RAM, single-player built co-op-ready, 16 km+ world, double precision). Leave ADR 0008 Proposed until the owner accepts it. No full editor or content suite work before the roadmap gates.
