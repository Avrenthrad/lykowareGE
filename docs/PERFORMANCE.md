# Performance and validation contract

Status: desktop and handheld budgets below remain proposals for owner review, not measured device guarantees. A provisional CPU-only CTest check is active for the pinned hosted runners windows-2022 and ubuntu-24.04. It is not a hardware profile, not a frame-time or memory gate, and it does not satisfy the image/performance-gate checklist item.

## Proposed profiles

| Metric | RTX 4070 desktop proposal | MSI Claw proposal |
| --- | --- | --- |
| Sustained gameplay target | 60 FPS (16.67 ms/frame) | 30 FPS (33.33 ms/frame) |
| p99 frame time | At most 1.5 times target frame interval | At most 1.5 times target frame interval |
| Traversal stalls | No frame over 100 ms in a warmed 10-minute fixture | Same fixture with handheld quality tier |
| Unit test scenario | 256 active simulated units | Same rules; quality/AI update tiers may differ |

Exact MSI Claw model, CPU/RAM/VRAM/shared-memory limits, TDP, OS/driver, resolution, quality tiers, world/traversal workload and story-state size remain unconfirmed. GPU memory, streaming bandwidth and inference budgets must be selected before their phase gates can pass. Desktop and handheld parity means coherent gameplay, not identical settings or local LLM throughput.

## Measurement method

Pin fixture version, random seed, toolchain, driver, device, power mode, resolution, camera path and warmup. Report cold start separately from warmed traversal. Capture CPU/GPU frame-time distributions, hitches, peak/resident memory, upload/decompression work, shader/pipeline misses and loaded asset counts.

Generic hosted CI can enforce unit/CPU tests and agreed CPU budgets on a pinned runner class. Device-specific GPU gates require calibrated runners or reproducible local hardware reports; arbitrary hosted timings are not a valid handheld/4070 baseline. Record threshold changes in reviewed configuration rather than silently weakening failures.

## Provisional hosted CPU check

The root `CMakeLists.txt` is the reviewed configuration. It passes three numbers into CTest. There is no TOML or JSON budget file.

| Check | CMake variable | Value | Measurement |
| --- | --- | --- | --- |
| perf.sandbox_startup | LYKOWARE_PROVISIONAL_CPU_SANDBOX_STARTUP_MS | 20000 | Wall time to launch `lykoware_sandbox` with no arguments and wait for it to exit |
| perf.cpu_fixture | LYKOWARE_PROVISIONAL_CPU_FIXTURE_ITERATIONS and LYKOWARE_PROVISIONAL_CPU_FIXTURE_MS | 1000000 iterations and 20000 ms | In-process loop of `cpu_fixture_iteration`, the same step each sandbox frame runs |

The check fails when elapsed microseconds are greater than the millisecond limit times 1000. `perf.budget_rejects_overrun` locks that comparison, so a broken check does not pass by ignoring the limit. Margins are wide on purpose. A local GCC 13.3.0 Debug run measured sandbox startup at about 1 ms and one million iterations at about 323 ms, so 20000 ms is slack for a busy shared runner rather than a frame budget. These numbers are valid only for windows-2022 and ubuntu-24.04. They are CPU-only. They are not an RTX 4070 or MSI Claw result.

## Testing layers

- Unit checks for algorithms and validation.
- Integration checks for lifecycle, commands, loading and errors.
- Fixed-seed simulation/replay checks within a declared determinism scope.
- Screenshot/golden-image checks once a renderer exists, with documented tolerances per supported backend/device tier.
- Stress, memory, streaming and sustained performance fixtures as those systems arrive.
- Local inference tests with async deadlines, cancellation, bounded output validation and fallback behavior.

"Zero stutter" guides engineering. Precompiled shaders, cache invalidation, background streaming and bounded workloads reduce causes; they do not prove universal hitch-free operation. Record failed hardware runs and known limits rather than declaring the goal achieved.
