# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).
Current focus: Phase 0 native C++ tooling and unresolved graphics/physics/data contracts. C++20/CMake is accepted in ADR 0001. No implementation task owner/branch assigned yet.
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

Extend the accepted C++20/CMake foundation with minimal lyko build/test tooling and planned sanitizer/static-analysis checks. Resolve graphics/physics/data ADRs before their dependent implementations. Obtain multiplayer and hardware answers while preparing representative tests. No full editor or content suite work before the roadmap gates.
