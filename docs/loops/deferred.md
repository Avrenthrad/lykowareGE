# Parked integration and owner checks

The owner explicitly requested continuing the overall project while saving anything needing later attention. Keep progressing on separate task branches; do not merge or push to main under the standing restriction. Dependency branches are preserved and PR bases state the order.

| Item | Evidence / dependency | Resume condition |
|---|---|---|
| PR #71 authored animation/events/sockets/retarget | `7197587`, all five CI checks run42481306674; native 79/79 each, zero skips; reviewed | Owner permits integration; then merge verified exact head before dependent PRs |
| Phase1 target hardware 01.09–01.12 | RTX4070 owner-reported only; existing local measurement/assessment tooling is available | Owner's Windows PC accessible for VRAM/driver, pacing/hitches/RAM/VRAM and calibrated budgets |
| Windows actual graphics / character feel 02.15–02.16 | Hosted graphics skip; software Vulkan and CPU checks do not close physical-PC acceptance | Actual PC execution and owner play review |
| Broad foot IK / ragdoll gates | Stationary orientation/pelvis #72 and walking position/normal anchors #73 are verified, unmerged | Complete each runtime module and its tests; foot heading, support render interpolation, balance and ragdolls remain future requirements |
| Public release/Pax/legal and paid services | No public release or spend authorized | Explicit owner authorization and required review |

Current verified dependency order: #71 `7197587` → #72 `e21f797` → #73 runtime `b7dbd153`. All remain pending owner-authorized integration. #74 reconciles status on a separate main-based documentation branch. No further in-scope implementation blocker remains.
