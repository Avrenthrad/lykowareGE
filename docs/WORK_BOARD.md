# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).
Current focus: Phase 0 native C++ tooling. C++20/CMake (ADR 0001), Vulkan 1.3 behind an owned RHI (ADR 0002) and canonical JSON / TOML / glTF 2.0 (ADR 0004) are accepted. Physics remains proposed in ADR 0003. No implementation task owner/branch is assigned yet. The graphics and data-format acceptance is recorded below.
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

- Task, branch, commit, PR: accept ADR 0002 and ADR 0004; branch `docs/accept-adr-0002-0004`; commit and PR recorded when this change is published.
- Completed behavior: ADR 0002 and ADR 0004 are Accepted. The brief no longer evaluates wgpu against ash, no longer puts Jolt behind bindings, and no longer lists RON as a format.
- Files/interfaces changed: documentation listed in the task. No public API or on-disk schema was implemented.
- Checks run and actual results: link and consistency review of the edited docs. CI results belong in the PR once the run finishes; do not treat this note as a green build.
- Unverified behavior/platforms: no renderer, capture path, parser round-trip or vendored library was exercised. Linux and Windows builds are unchanged in source and still need the PR's CI.
- Open decisions and blockers: ADR 0003 physics; multiplayer and hardware profiles; shader-compiler licence; lavapipe (or another software Vulkan driver) as a CI follow-up; exact parser choice at first use.
- Next concrete action: keep Phase 0 on the lyko build/test CLI and planned sanitizer/static-analysis coverage. Do not vendor graphics or parser dependencies until a caller needs them.
- Checklist/documentation updates: graphics and data-format acceptance is checked. Physics acceptance stays open. STATUS next steps match this handoff.

## Suggested next task

Extend the accepted C++20/CMake foundation with minimal lyko build/test tooling and planned sanitizer/static-analysis checks. Resolve ADR 0003 before adding physics. Obtain multiplayer and hardware answers while preparing representative tests. When rendering work starts, vendor pinned Vulkan-Headers, volk and Vulkan Memory Allocator under third_party and review the shader-compiler licence first. No full editor or content suite work before the roadmap gates.
