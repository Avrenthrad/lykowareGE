# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).
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

Develop Phase 1 window/input/timing. Resolve graphics/physics/data ADRs before their dependent implementations. Obtain multiplayer and hardware answers while preparing representative tests. No full editor or content suite work before the roadmap gates.
