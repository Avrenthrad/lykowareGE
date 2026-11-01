# Lykoware status

## Done and verified

- Original Module 01: C++20/CMake static engine library, separate console sandbox, logging and executable smoke checks.
- Windows Debug/Release clean builds, staged execution, and Linux validation passed: [baseline implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f), [CI run](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
- C++20/CMake accepted by Joshua in ADR 0001; no language migration required.
- Graphics accepted in ADR 0002: Vulkan 1.3 used directly from C++ behind a Lykoware-owned RHI. wgpu and ash are rejected. No graphics dependency is vendored.
- Data formats accepted in ADR 0004: canonical JSON for machine-written data, TOML for hand-edited config, glTF 2.0 for meshes, binary only as cooked output. RON is dropped. No parser is vendored.
- The brief's stack section now matches those ADRs, including native C++ Jolt rather than bindings. Jolt itself is still proposed in ADR 0003.
- Planning documents express the owner-supplied vision, full-ownership policy, phased roadmap and AI-native tooling requirements. Documentation is not runtime implementation.

## Current work

Phase 0: continue the verified C++20/CMake foundation. ADRs 0001, 0002 and 0004 are accepted. ADR 0003 (physics) remains proposed. The earlier language-comparison task is no longer required.
Repository remains Avrenthrad/lykowareGE; no rename requested or performed.
Read [ROADMAP.md](ROADMAP.md), [ADRs](adr/README.md) and [WORK_BOARD.md](WORK_BOARD.md).

## Next

1. Resolve multiplayer intent and exact hardware/performance profiles.
2. Extend native tooling with a minimal lyko build/test CLI and add the planned sanitizer/static-analysis coverage.
3. Resolve Jolt integration through ADR 0003. If Jolt is adopted, call it natively from C++ and wrap it per ADR 0000. Do not vendor Vulkan-Headers, volk, Vulkan Memory Allocator, nlohmann/json or toml++ until a caller needs them; record exact licences and versions then. Review the shader-compiler licence as a build-time tool, and evaluate a software Vulkan driver such as Mesa lavapipe in CI, before claiming graphics CI.
4. Progressively add real capture/performance gates with the systems they test. Headless capture must render offscreen through the same RHI path.

## Known issues and incomplete capabilities

- No engine window, renderer, ECS, scene loader, physics, audio, animation, world streaming or local inference exists.
- No lyko executable, screenshot/trace commands or performance gates exist yet. No Cargo workspace is planned.
- The old successful CI run proves only the console foundation; it does not validate new performance goals.
- Capture golden tests require a renderer; hardware budgets need calibrated machines and fixtures.
- C++20, the Vulkan 1.3 RHI direction and the data encodings are accepted decisions only. No renderer, parser or third-party source has been added. No Rust prototype or comparative benchmark was performed. wgpu, ash and RON are not open tasks.
- Shared collaboration and planning documents are on main; the earlier cancelled documentation update is resolved.
- Pax/publication gate is policy only; no Pax software or legal clearance exists.
- No spend or third-party AI-service invocation has been authorized by this documentation work.

## Handoff rule

Update this file with actual completed checks, active task/owner/branch, blockers and next actions when implementation changes. Follow WORK_BOARD templates; never describe planned systems as complete.
