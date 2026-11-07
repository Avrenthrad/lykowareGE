# Ten-step batch: stationary terrain foot alignment

Implemented and locally verified on 7 November 2026, in draft [#64](https://github.com/Avrenthrad/lykowareGE/pull/64), stacked on #63. These checked items certify this bounded implementation/Linux-verification batch, not merge, Windows readiness or the full foot-IK milestone. Design: [ADR 0059](adr/0059-stationary-foot-placement.md).

- [x] **S01 — Calibrate mapped legs.** Retain a validated skeleton and roles; refuse collapsed/stretched/unsupported direct chains, non-unit scales and interdependent leg branches.
- [x] **S02 — Bridge coordinate spaces.** Double-space actor/model conversion includes facing, uniform visual scale and fitting offset. Round-trip and actual fitted/yawed 64 km terrain-contact checks pass; out-of-range physics ray origins are refused.
- [x] **S03 — Add explicit profile configuration.** `foot_ik: true` requires mapped bones. Optional `foot_clearance` is 0..0.3 world metres after fitting (default 0.03). Type/dependency/range refusals and custom clearance are checked.
- [x] **S04 — Retain resolved roles in playback.** Imported leg indices are passed into the player's alignment controller; unmapped/default playback stays unchanged. Generated seven-joint CLI fixture verifies calibration and unsupported-rig refusal.
- [x] **S05 — Probe real physics terrain.** Read-only vertical rays cover flat and raised surfaces. Physics state hash remains unchanged by pose/contact updates.
- [x] **S06 — Bound contacts.** Probe from 0.35 m above the animated foot, 1 m down; refuse corrections over 0.3 m. An actual hit inside probe range but outside correction limits is rejected.
- [x] **S07 — Smooth and reset.** Exponential vertical correction agrees at 30/60 Hz for fixed contacts. Existing corrections clear after lost ground, teleport, inactivity and invalid/long frame deltas.
- [x] **S08 — Commit both legs transactionally.** Second-leg failure cannot publish the first leg's pose or contact state. Runtime errors retain base animation and reset alignment.
- [x] **S09 — Report applied results.** Diagnostics count successful updates, contacts, misses, clamping, resets, runtime failures and changed palettes copied to ECS rendering. CLI requires contacts, zero failures and applied frames; a deliberately omitted palette update is caught.
- [x] **S10 — Verify and document.** Frozen source `2cc97b5dc055323a14db12a952f940f524c1a8aa` passes 68/68 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips, clean unchanged source; configure/build/install/installed CLI pass. Additional imported-player CLI passes with synchronization validation enabled. Five external faults were caught: missing coordinate bridge, ignored correction bound, stale teleport state, partial bilateral publish, and omitted corrected render palette. Links/whitespace checked; master checklist and handoff updated.

Raw report: `/workspace/lykoware-foot-placement-validation/report.json`. Source fingerprint: `278304d59bfa728ca68305b7534f1f179bf15731bda778dfb6dc6ec908e9706c`. GCC 14.2, CMake/CTest 3.31.6, Mesa lavapipe LLVM 19; analysis uses GCC with clang-tidy 18. Supplementary synchronization check uses the local Khronos 1.4.309 development layer recorded in ADR 0052. Raw/generated/fault artifacts remain outside Git. Subsequent handoff edits change documentation only.

## Remaining acceptance gates

- [ ] Required hosted checks and Windows Debug/Release execution.
- [ ] Physical target-PC quality/performance measurements.
- [ ] Owner review and merge.

## Deliberate limits and next work

This is stationary vertical **foot-joint** alignment, active only when grounded, nearly still and visible. It does not infer sole geometry: clearance must match the rig's ankle/contact convention. It has no horizontal foot locks, walking stance phases, surface-normal orientation, slope classification, moving-platform anchoring, pelvis balancing or joint limits. Unreachable targets clamp without stretching and are counted; contact is not guaranteed when the leg cannot reach it. Unit joint scales/direct links remain required. Reference pose validation and temporary allocations remain; no allocation-free/crowd/zero-stutter budget claim.

Prepare reusable runtime chains/scratch and measure costs before scaling this path. Further foot IK needs orientation/stance control, body constraints and broader rig/terrain tests. The parent milestone remains open in [MASTER_CHECKLIST](MASTER_CHECKLIST.md).
