# Lykoware build checklist

Canonical scope: [ROADMAP.md](docs/ROADMAP.md). Actual implementation status: [STATUS.md](docs/STATUS.md).
This replaces the old module ordering while preserving verified Module 01 history.

## Verified baseline (former Module 01)

- [x] C++20/CMake engine and separate sandbox targets.
- [x] Logging, version/help and startup error checks.
- [x] Windows build instructions and automated Windows/Linux builds.
- [x] Windows Debug/Release smoke tests and staged execution ([evidence](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221)).

## Phase 0 — Foundations

- [x] Record owner vision, engine-first sequencing and ownership policy.
- [x] Prepare shared contributor instructions, handoffs, STATUS and ROADMAP.
- [x] Record language/graphics/physics/data proposals in docs/adr.
- [x] Add proprietary notice and dependency-vendoring requirements.
- [ ] Confirm final repository name before any rename (keep existing name meanwhile).
- [x] Accept C++20/CMake as the runtime/native-tooling stack with Joshua (ADR 0001; owner decision, no comparative benchmark claimed).
- [x] Accept graphics (ADR 0002: Vulkan 1.3 C++ behind an owned RHI) and data/command formats (ADR 0004: canonical JSON, TOML config, glTF 2.0 meshes). Decision and current dependencies are recorded separately in STATUS.
- [x] Accept Jolt v5.6.0 integration (ADR 0003, owner decision 2026-11-04; native C++ API in ADR 0020).
- [x] Agree desktop/RTX 4070 direction, 16 GB RAM, single-player built co-op-ready and double-position 16 km+ scope (ADR 0021).
- [ ] Calibrate concrete CPU/GPU/VRAM/frame-pacing budgets on target hardware.
- [x] Extend the C++20/CMake foundation with real lyko build/test commands (branch implementation; see STATUS for platform evidence).
- [x] Verify warnings-as-errors, sanitizer/static-analysis and CLI integration CI matrix ([all five profiles passed](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325853657)).
- [ ] Introduce actual image/performance gates as renderer and calibrated fixtures become available.
- [ ] Pass the full Phase 0 exit criteria.

## Runtime

- [ ] Phase 1 — Core runtime and headless/command/capture path.
- [ ] Phase 2 — Character feel on terrain.
- [ ] Phase 3 — Combat.
- [ ] Phase 4 — Structural building/crafting.
- [ ] Phase 5 — RTS faction battle.
- [ ] Phase 6 — World streaming at scale; engine core milestone.
- [ ] Phase 8 — Living Memory and relationships.
- [ ] Phase 9 — Local Living World AI.
- [ ] Phase 10 — Story/consequence systems.
- [ ] Phase 11 — Content pipeline and flagship vertical slice.

Phase 7's editor work has moved to Track E.

## Track E — After Phase 6

- [ ] E1 — Extend shared command bus for editing/undo.
- [ ] E2 — Custom shell, viewport/tree/inspector, undo/redo, reload.
- [ ] E3 — Terrain/world/building and animation/physics tools.
- [ ] E4 — Faction/memory/story/quest/dialogue tools.
- [ ] E5 — Asset/agent/profiling tools.

## Track C — After E2

- [ ] C1 — Mesh import/inspection and procedural tools.
- [ ] C2 — Polygon modelling/cleanup/retopology/LOD.
- [ ] C3 — Sculpting/UV/texturing/painting/baking.
- [ ] C4 — Rigging/skinning/animation/retargeting/IK.
- [ ] C5 — VFX/terrain/audio tools.

Every stage requires the roadmap's command parity, round-trip, quality and performance evidence. A prepared plan is not an implemented system.

## Core review follow-ups

Tracked with exit evidence in [REVIEW_FOLLOWUP.md](docs/REVIEW_FOLLOWUP.md). Branch implementations alone do not check off the Phase 1 gate.
