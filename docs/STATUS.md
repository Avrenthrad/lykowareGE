# Lykoware status

## Done and verified

- Original Module 01: C++20/CMake static engine library, separate console sandbox, logging and executable smoke checks.
- Windows Debug/Release clean builds, staged execution, and Linux validation passed: [baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f), [CI run](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
- C++20/CMake accepted by Joshua in ADR 0001; no language migration required.
- Planning documents now express the owner-supplied vision, full-ownership policy, phased roadmap, AI-native tooling requirements and proposed decisions. Documentation is not runtime implementation.

## Current work

Phase 0: continue the verified C++20/CMake foundation. Joshua selected C++20; ADR 0001 is accepted. The earlier comparison task is no longer required.
Repository remains Avrenthrad/lykowareGE; no rename requested or performed.
Read [ROADMAP.md](ROADMAP.md), [ADRs](adr/README.md) and [WORK_BOARD.md](WORK_BOARD.md).

## Next

1. Resolve multiplayer intent and exact hardware/performance profiles.
2. Extend native tooling with a minimal lyko build/test CLI and add the planned sanitizer/static-analysis coverage.
3. Resolve graphics, Jolt integration and data/command formats through ADRs 0002–0004.
4. Progressively add real capture/performance gates with the systems they test.

## Known issues and incomplete capabilities

- No engine window, renderer, ECS, scene loader, physics, audio, animation, world streaming or local inference exists.
- No lyko executable, screenshot/trace commands or performance gates exist yet. No Cargo workspace is planned.
- The old successful CI run proves only the console foundation; it does not validate new performance goals.
- Capture golden tests require a renderer; hardware budgets need calibrated machines and fixtures.
- C++20 is accepted; no Rust prototype or comparative benchmark was performed. No dependencies have been evaluated/vendored.
- Shared collaboration and planning documents are on main; the earlier cancelled documentation update is resolved.
- Pax/publication gate is policy only; no Pax software or legal clearance exists.
- No spend or third-party AI-service invocation has been authorized by this documentation work.

## Handoff rule

Update this file with actual completed checks, active task/owner/branch, blockers and next actions when implementation changes. Follow WORK_BOARD templates; never describe planned systems as complete.
