# Performance and validation contract

Status: the desktop budgets below remain proposals for owner review, not measured device guarantees. Provisional CPU-only CTest checks, including a physics body-count benchmark, are active for the pinned hosted runners windows-2022 and ubuntu-24.04. They are not a hardware profile, not a frame-time or memory gate, and they do not satisfy the image/performance-gate checklist item.

## Proposed profiles

Owner decisions ([ADR 0021](adr/0021-product-targets.md), 2026-11-04): desktop PC only; the MSI Claw is no longer a target. The RTX 4070 is the recommended PC at 1440p and 60 FPS. A weaker minimum PC (about RTX 3060 / RX 6600 class, set by measurement) runs with lower settings. 16 GB of system RAM is the minimum.

| Metric | Recommended: RTX 4070, 1440p | Minimum: about RTX 3060 / RX 6600 |
| --- | --- | --- |
| Sustained gameplay target | 60 FPS (16.67 ms/frame) | 60 FPS at lower settings (proposal) |
| p99 frame time | At most 1.5 times target frame interval | Same |
| Traversal stalls | No frame over 100 ms in a warmed 10-minute fixture | Same fixture at minimum settings |
| Unit test scenario | 256 active simulated units | Same rules; quality and AI update tiers may differ |
| System RAM | Game and local AI within about 10–12 GB of a 16 GB machine | Same |
| VRAM | 12 GB card | Minimum settings within 8 GB (proposal) |

Desktop CPU (proposed: 6 cores, 12 threads, AVX2), the exact minimum GPU, OS and driver versions, world and traversal workload, and story-state size remain unconfirmed. GPU memory, streaming bandwidth and inference budgets must be selected before their phase gates can pass. Only desktop PC is targeted (ADR 0021).

## Measurement method

Pin fixture version, random seed, toolchain, driver, device, power mode, resolution, camera path and warmup. Report cold start separately from warmed traversal. Capture CPU/GPU frame-time distributions, hitches, peak/resident memory, upload/decompression work, shader/pipeline misses and loaded asset counts.

Generic hosted CI can enforce unit/CPU tests and agreed CPU budgets on a pinned runner class. Device-specific GPU gates require calibrated runners or reproducible local hardware reports; arbitrary hosted timings are not a valid recommended-PC baseline. Record threshold changes in reviewed configuration rather than silently weakening failures.

## Provisional hosted CPU check

The root `CMakeLists.txt` is the reviewed configuration. It passes three numbers into CTest. There is no TOML or JSON budget file.

| Check | CMake variable | Value | Measurement |
| --- | --- | --- | --- |
| perf.sandbox_startup | LYKOWARE_PROVISIONAL_CPU_SANDBOX_STARTUP_MS | 20000 | Wall time to launch `lykoware_sandbox` with no arguments and wait for it to exit |
| perf.cpu_fixture | LYKOWARE_PROVISIONAL_CPU_FIXTURE_ITERATIONS and LYKOWARE_PROVISIONAL_CPU_FIXTURE_MS | 1000000 iterations and 20000 ms | In-process loop of `cpu_fixture_iteration`, the same step each sandbox frame runs |

The check fails when elapsed microseconds are greater than the millisecond limit times 1000. `perf.budget_rejects_overrun` locks that comparison, so a broken check does not pass by ignoring the limit. Margins are wide on purpose. A local GCC 13.3.0 Debug run measured sandbox startup at about 1 ms and one million iterations at about 323 ms, so 20000 ms is slack for a busy shared runner rather than a frame budget. These numbers are valid only for windows-2022 and ubuntu-24.04. They are CPU-only. They are not an RTX 4070 or MSI Claw result.

### Physics body count

`lykoware_physics_benchmark BODIES TICKS LIMIT_MS` drops layers of 16 x 16 boxes (0.5 m cubes) onto the ground, so they collide and pile up. It steps the world TICKS times at 60 Hz and times each step. It prints the mean, 95th-percentile (p95) and worst step times and how many boxes still move at the end. It fails if a step fails, a box falls through the ground, or the mean step time is over LIMIT_MS.

| Check | CMake variables | Value | Measurement |
| --- | --- | --- | --- |
| perf.physics_bodies_256 | LYKOWARE_PROVISIONAL_PHYSICS_TICKS and LYKOWARE_PROVISIONAL_PHYSICS_MEAN_STEP_MS | 300 ticks, 250 ms mean per step | 256 boxes |
| perf.physics_bodies_1024 | the same | the same | 1024 boxes |
| perf.physics_bodies_1024_far | the same | the same | 1024 boxes 16 km from the origin (ADR 0022) |

The 250 ms limit catches large regressions in Debug and sanitizer builds on a busy shared runner. It is not a frame budget. Local results on 4 hardware threads with GCC 13.3, 300 ticks, ms per step:

| Build | Boxes | Mean | p95 | Worst |
| --- | --- | --- | --- | --- |
| Release | 256 | 0.08 | 0.43 | 1.09 |
| Release | 1024 | 0.72 | 2.60 | 4.06 |
| Release | 4096 | 9.86 | 12.84 | 27.84 |
| Debug | 256 | 0.59 | 3.19 | 5.76 |
| Debug | 1024 | 6.26 | 22.91 | 35.89 |
| ASan/UBSan | 1024 | 14.6 | 53.8 | 74.3 |

For scale, a 60 Hz frame is 16.67 ms. In Release, 1024 colliding boxes use about 4% of it on average and a quarter of it in the worst step. The worst steps come early, while the pile is falling and most contacts are new. These are numbers from one local machine, not a hardware profile. CTest does not print a passing test's output, so the hosted runners' per-step numbers are not in the CI logs, but each test's total time (setup plus 300 steps) bounds them. In [run 41771495786](https://github.com/Avrenthrad/lykowareGE/actions/runs/41771495786) the 1024-box test took 0.27 s on Windows Release (under 0.9 ms per step), 7.9 s on Windows Debug (under 27 ms) and 12.4 s under Linux ASan/UBSan (under 42 ms).

The table and timings above are for single precision. With double-precision positions ([ADR 0022](adr/0022-double-precision-positions.md)), Release is about 10–15% slower with many contacts: 1024 boxes mean 0.81–0.83 ms, worst 4.1–5.6 ms; 4096 boxes mean 10.9–11.3 ms. 256 boxes are unchanged.

### Foot IK and palettes at crowd scale (02.11b-runtime-scale)

`lykoware_ik_runtime_scale [--characters N] [--frames F] [--csv PATH]` poses three rigs (mannequin 12 joints, humanoid 31, dense 249) through the prepared two-bone solver (ADR 0061), two legs per character per frame, and rebuilds each character's retained skinning palette. CSV columns: `rig,joints,characters,frames,solves,allocations,mean_ns_per_solve,worst_frame_ms,mean_palette_ns`. CI (`animation.ik_runtime_scale`, 32 characters × 60 frames) fails on a foot off target by more than 0.5 mm (FK check) or on any heap allocation in the measured frames; it does not judge time. Target-PC command for the harness: `lykoware_ik_runtime_scale --characters 500 --frames 600 --csv ik_scale.csv` from the Release build. The owner runs it on the RTX 4070 PC and accepts the numbers.

### ECS iteration

`lykoware_ecs_benchmark ENTITIES FRAMES LIMIT_MS` runs a movement system (position += velocity × dt) over ENTITIES entities each frame and fails if the mean frame time is over the limit or the result is wrong. CTest runs `perf.ecs_iterate_100k`: 100,000 entities, 300 frames, 250 ms limit. That limit is slack for Debug and sanitizer builds, not a frame budget.

Local, GCC 13 Release, one thread: about 4 ns per entity per frame. 1000 entities take 0.005 ms, 100,000 take 0.39 ms and 1,000,000 take 3.9 ms. Debug is about 63 ns per entity. These numbers are from one local machine, not a hardware profile.

## Testing layers

- Unit checks for algorithms and validation.
- Integration checks for lifecycle, commands, loading and errors.
- Fixed-seed simulation/replay checks within a declared determinism scope.
- Screenshot/golden-image checks once a renderer exists, with documented tolerances per supported backend/device tier. The Phase 0 image harness compares a software test pattern with committed PPM fixtures. It is not this gate.
- Stress, memory, streaming and sustained performance fixtures as those systems arrive.
- Local inference tests with async deadlines, cancellation, bounded output validation and fallback behavior.

"Zero stutter" guides engineering. [ADR 0002](adr/0002-graphics.md) accepts ahead-of-time SPIR-V and device/driver-keyed pipeline caches as the approach. Precompiled shaders, cache invalidation, background streaming and bounded workloads reduce causes; they do not prove universal hitch-free operation. Record failed hardware runs and known limits rather than declaring the goal achieved.

## Repeatable scene measurement

The owner supplied Windows 11 Home build 26300, Intel Core i7-14700F (20 cores / 28 logical processors), and 16 GB RAM. The owner reports an NVIDIA RTX 4070; exact VRAM/driver remain unmeasured. No personal host/user identifiers are recorded. This is context for a future local run, not measured performance.

From a Visual Studio developer PowerShell with CMake, Python and Git available:

```powershell
./tools/validation/measure-scene.ps1
```

The runner builds/tests Release, records CPU/OS/RAM and GPU driver metadata, then runs the same versioned box scene/commands three times at 2560×1440, warming 120 draws and measuring 600 simulation+offscreen-draw frames. Local outputs stay in a new ignored subdirectory of `build/measurements`; nothing uploads. `-SkipBuild`, `-Executable PATH`, `-OutputDirectory PATH` and `-Frames N` customize it. A software GPU is explicitly refused as hardware evidence.

Each report contains the simulation replay hash, final hash/tick, active entities, CPU simulation percentiles, end-to-end synchronous offscreen frame percentiles/raw samples, device/driver and peak process resident bytes. Capture is a PPM of the real renderer. Timings exclude pipeline creation and final screenshot readback; snapshot preparation and GPU completion wait are included. These times are not GPU timestamp measurements or swapchain frame pacing. GPU timestamps and VRAM residency are null rather than invented. This four-box fixture cannot establish the flagship or 16 km streaming budget; the recommended-profile gate remains open until the actual PC report and later representative loads are reviewed.

See [hardware evidence and assessment](LOCAL_VALIDATION.md#rtx-4070-evidence-and-assessment) for version 2 GPU inventory, integrity checks, raw-sample percentiles and optional explicitly provisional fixture-bound budget enforcement. No calibrated target-hardware budget is claimed.
