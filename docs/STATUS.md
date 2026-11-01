# Lykoware status

## Done and verified

- Original Module 01: C++20/CMake static engine library, separate console sandbox, logging and executable smoke checks.
- Windows Debug/Release clean builds, staged execution, and Linux validation passed: [baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f), [CI run](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
- Planning documents now express the owner-supplied vision, full-ownership policy, phased roadmap, AI-native tooling requirements and proposed decisions. Documentation is not runtime implementation.

## Current work

Phase 0: reconcile requirements and compare C++20 with Rust before choosing. Joshua explicitly requested comparison. Preserve the working C++ baseline during evaluation.
Repository remains Avrenthrad/lykowareGE; no rename requested or performed.
Read [ROADMAP.md](ROADMAP.md), [ADRs](adr/README.md) and [WORK_BOARD.md](WORK_BOARD.md).

## Next

1. Resolve multiplayer intent and exact hardware/performance profiles.
2. Execute the bounded language comparison described in ADR 0001; record evidence and decide with Joshua.
3. Resolve graphics, Jolt integration and data/command formats through ADRs 0002–0004.
4. Implement the chosen foundation and minimal lyko CLI; progressively add real capture/performance gates with the systems they test.

## Known issues and incomplete capabilities

- No engine window, renderer, ECS, scene loader, physics, audio, animation, world streaming or local inference exists.
- No lyko executable, screenshot/trace commands, Cargo workspace or performance gates exist yet.
- The old successful CI run proves only the console foundation; it does not validate new performance goals.
- Capture golden tests require a renderer; hardware budgets need calibrated machines and fixtures.
- Rust is a proposal, not an accepted replacement. No dependencies have been evaluated/vendored.
- Prior collaboration documentation was prepared in an unmerged commit after a cancelled main update. This change includes the reconciled documents so all contributors can read the same version when published.
- Pax/publication gate is policy only; no Pax software or legal clearance exists.
- No spend or third-party AI-service invocation has been authorized by this documentation work.

## Handoff rule

Update this file with actual completed checks, active task/owner/branch, blockers and next actions when implementation changes. Follow WORK_BOARD templates; never describe planned systems as complete.
