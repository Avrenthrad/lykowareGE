# Parked integration and owner checks

The owner explicitly requested continuing the overall project while saving anything needing later attention. Keep progressing on separate task branches. Merges to main need the owner's say-so; on 10 November 2026 the owner authorized merging #71–#74, which is done. Dependency branches are preserved and PR bases state the order.

| Item | Evidence / dependency | Resume condition |
|---|---|---|
| Phase1 target hardware 01.09–01.12 | RTX4070 owner-reported only; existing local measurement/assessment tooling is available | Owner's Windows PC accessible for VRAM/driver, pacing/hitches/RAM/VRAM and calibrated budgets |
| Windows actual graphics / character feel 02.15–02.16 | Hosted graphics skip; software Vulkan and CPU checks do not close physical-PC acceptance | Actual PC execution and owner play review |
| Broad foot IK / ragdoll gates | Stationary orientation/pelvis (#72, `58e170e`) and walking position/normal anchors (#73, `6cabcf3`) are merged | Complete each runtime module and its tests; foot heading (ADR 0068, #75) and drawn-support interpolation (ADR 0069, #76) are merged; balance and ragdolls remain future requirements |
| Public release/Pax/legal and paid services | No public release or spend authorized | Explicit owner authorization and required review |

The dependency order #71 → #72 → #73 was merged on 10 November 2026 as `35ad909`, `58e170e` and `6cabcf3`, after the integrated tree passed 82/82 in each local Linux profile; #74 records it. No further in-scope implementation blocker remains.
