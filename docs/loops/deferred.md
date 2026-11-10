# Parked integration and owner checks

The owner explicitly requested continuing the overall project while saving anything needing later attention. Keep progressing on separate task branches; do not merge or push to main under the standing restriction. Dependency branches are preserved and PR bases state the order.

| Item | Evidence / dependency | Resume condition |
|---|---|---|
| PR #71 authored animation/events/sockets/retarget | `7197587`, all five CI checks run42481306674; native 79/79 each, zero skips; reviewed | Owner permits integration; then merge verified exact head before dependent PRs |
| Phase1 target hardware 01.09–01.12 | RTX4070 owner-reported only; existing local measurement/assessment tooling is available | Owner's Windows PC accessible for VRAM/driver, pacing/hitches/RAM/VRAM and calibrated budgets |
| Windows actual graphics / character feel 02.15–02.16 | Hosted graphics skip; software Vulkan and CPU checks do not close physical-PC acceptance | Actual PC execution and owner play review |
| Broad foot IK / ragdoll gates | Stationary vertical placement and prepared solvers exist; orientation/pelvis work is current | Complete each runtime module and its tests; walking locks/ragdoll remain future requirements |
| Public release/Pax/legal and paid services | No public release or spend authorized | Explicit owner authorization and required review |

Current engineering dependency: `codex/terrain-foot-constraints` based on #71; no conflicting implementation owner found in open PRs. User scope now extends past the previous 15-item batch, while preserving that batch's integration state.
