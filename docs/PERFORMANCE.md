# Performance and validation contract

Status: budget proposals for owner review, not measured guarantees or active CI gates.

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

## Testing layers

- Unit checks for algorithms and validation.
- Integration checks for lifecycle, commands, loading and errors.
- Fixed-seed simulation/replay checks within a declared determinism scope.
- Screenshot/golden-image checks once a renderer exists, with documented tolerances per supported backend/device tier.
- Stress, memory, streaming and sustained performance fixtures as those systems arrive.
- Local inference tests with async deadlines, cancellation, bounded output validation and fallback behavior.

"Zero stutter" guides engineering. Precompiled shaders, cache invalidation, background streaming and bounded workloads reduce causes; they do not prove universal hitch-free operation. Record failed hardware runs and known limits rather than declaring the goal achieved.
