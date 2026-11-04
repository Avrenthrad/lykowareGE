# ADR 0022 — Double-precision positions

Status: Proposed. The owner chose the direction ("double precision now") in [ADR 0021](0021-product-targets.md) on 2026-11-04; this ADR records how it is built and the evidence.
Related task/PR: double-precision spike. PR 1 (branch `claude/physics-double`) switches Jolt; PR 2 changes the public position type.
Supersedes / superseded by: supersedes, in part, [ADR 0018](0018-jolt-integration.md)'s `DOUBLE_PRECISION OFF` (ADR 0003's "float precision for now").

## Context

ADR 0021 sets one seamless world of 16 km or more. A `float` has 24 bits of precision, so the gap between neighbouring values grows with distance from the origin:

| Distance from origin | Gap between floats | Movement in one 60 Hz tick that rounds away to nothing |
| --- | --- | --- |
| 1 km | 0.06 mm | under 0.03 mm (under 1.8 mm/s) |
| 8 km (edge of a 16 km world centred on the origin) | 0.49 mm | under 0.24 mm (under 1.5 cm/s) |
| 16 km | 0.98 mm | under 0.49 mm (under 2.9 cm/s) |

So at the edge of the world, slow-moving or resting objects lose small motions every tick, contacts jitter and characters can stick. A `double` has 53 bits: at 16 km the gap is about 0.000000002 mm.

## Options

- **Double-precision positions** (Jolt's `JPH_DOUBLE_PRECISION`). Positions and position matrices become `double`. Velocities, rotations, shapes and most collision maths stay `float`. Slightly slower and more memory per body; no change to how game code works.
- **Single precision and a moving origin.** Keep `float` and shift every object when the player travels far. Fastest, but every system (physics, AI, audio, saves, networking for co-op) must handle the shift, and two co-op players far apart cannot both be near the origin.
- **Decide later.** Cheapest now; the switch gets more expensive as more code depends on `float`.

The owner chose double precision now (ADR 0021).

## Decision

Proposed:

- **Jolt build:** `cmake/VendorJolt.cmake` forces `DOUBLE_PRECISION ON`, alongside `CROSS_PLATFORM_DETERMINISTIC ON`. In this mode Jolt's `Real` is `double` and `RVec3` is `DVec3` (`Jolt/Math/Real.h`).
- **PR 1 (this branch):** only the inside of `engine/src/physics.cpp` changes. The public API keeps `float` positions for one more PR. The physics state hashes now include all 64 bits of every position, so determinism is checked at full precision.
- **PR 2 (next):** public positions become `double`: body and character positions, ray origins and hit points. Velocities, directions, sizes and rotations stay `float`. A test shows bodies and characters 16 km from the origin behave like ones at the origin.
- **Rendering** (later): GPUs work in `float`, so positions will be made relative to the camera before they reach the GPU (ADR 0021 working assumption).

## Contracts and compatibility

- The recorded determinism hashes change, because the simulation now rounds positions differently and the hashes cover more bits:

| Test | Old (float) | New (double) |
| --- | --- | --- |
| `physics.jolt_determinism` | `0x17e8264fc6fa9cd7` | `0x3ae5ee6746df5bad` |
| `physics.world` | `0x84f90a3cbf37a28b` | `0x38077780cc770c57` |
| `physics.character` | `0xbe1264481a8a8720` | `0x4879cae68e652c53` |

- The new values must match in every CI job, as before (ADR 0018).
- No public API change in PR 1. PR 2 changes `lykoware::physics` position types; it has no outside callers yet.
- The AVX2 CPU requirement (ADR 0018) is unchanged.

## Consequences

- Physics positions stay accurate to far better than a millimetre anywhere in a 16 km+ world, with no origin shifting to coordinate across systems or co-op players.
- Each position takes 24 bytes instead of 12, and the step is a little slower: see Verification.
- Rendering must become camera-relative when the 3D renderer arrives.

## Verification

- **Local (PR 1):**
  - GCC 13 Debug and Release and Clang 18 Debug and Release all produce the three new hashes.
  - All 30 tests pass in the Debug, ASan/UBSan and clang-tidy presets.
  - **Cost, benchmark** (GCC 13 Release, 4 threads, 300 ticks, ms per step; the float numbers are from the same machine earlier in the day):

| Boxes | Float mean / worst | Double mean / worst |
| --- | --- | --- |
| 256 | 0.08 / 1.09 | 0.08 / 0.87 |
| 1024 | 0.72 / 4.06 | 0.81–0.83 / 4.1–5.6 |
| 4096 | 9.86 / 27.8 | 10.9–11.3 / 29.2–35.3 |

  About 10–15% slower with many contacts. 1024 colliding boxes still use about 5% of a 60 FPS frame on average.
- **CI (PR 1):** [run 41786685034](https://github.com/Avrenthrad/lykowareGE/actions/runs/41786685034) on a5230ab passed all five jobs: Windows MSVC Debug and Release and Linux GCC Debug, ASan/UBSan and clang-tidy all reproduce the three new hashes.
