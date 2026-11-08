# Next 15 checklist items — owner request, 8 November 2026

Joshua requested the next 15 steps from the pinned master checklist. This selects the next actionable leaf/runtime entries in Phase 2; parent and hardware acceptance gates are retained. The four Phase 1 hardware gates and combined Windows/actual-GPU acceptance entry are deferred while Joshua is mobile. These are existing stable IDs, not fifteen newly invented completion claims. Seven entries already have merged/tested implementations; reconciling their stale boxes is explicitly distinguished from eight new engineering items.

| # | Master ID | Work / exit evidence | State |
|---|---|---|---|
| 1 | 02.01 | Shared conventions/validation and CPU CLI; #69, native 76/76 each + five CI profiles | Verified/merged |
| 2 | 02.02a | Bind-size/height/feet fitting; #59 → #65, later #68 all hosted profiles and native tests | Verified/merged |
| 3 | 02.02b | Authored humanoid roles/mapping; #60/#61 → #65, native rig/terrain checks | Verified/merged |
| 4 | 02.02c | Visual yaw, explicit capsule/eye settings and render/IK checks; #69 | Verified/merged |
| 5 | 02.02d | Explicit named/topology/scale/rest-direction/axis diagnostics; #69 | Verified/merged |
| 6 | 02.03a | Versioned model/clip/height profiles; #60 → #65 with selector/refusal coverage | Verified/merged |
| 7 | 02.03b | Mapped roles and remaining six-state clip selectors; #61/#68 | Verified/merged |
| 8 | 02.04-runtime | Six-state presentation and real Jolt adapter; #68, all five CI + 74/74 per native profile | Verified/merged |
| 9 | 02.05-crossfade | Interruptible generic crossfade; #68, allocation/interruption/transaction tests | Verified/merged |
| 10 | 02.06 | Turning/terrain movement behavior with slope/step/uneven-ground fixtures | Open |
| 11 | 02.07 | Explicit controller/root-motion policy and executable policy checks | Open |
| 12 | 02.08 | Reusable authored animation configuration consumed by shared runtime paths | Open |
| 13 | 02.09 | Animation events and attachment sockets; interval/wrap/transform tests | Open |
| 14 | 02.10 | Retargeting for a declared supported rig subset; rest/axis/length and refusal tests | Open |
| 15 | 02.11a | Two-bone geometry reference; #62 → #65, analytical/varied/mutant checks, later matrix verified | Verified/merged |

Progress: **10/15 verified and merged; 5 open.** Each new implementation needs a task branch/ADR, meaningful native tests, Windows Debug/Release and Linux CI, and authorized merge before the entry is checked. Preserve contributor branches and record source hashes/evidence in STATUS/PRs. Recheck this list at each handoff; do not silently replace an item with a smaller requirement.

## Deferred owner/manual list

- Run target-PC validation/inventory; RTX 4070 is owner-reported, exact VRAM/driver unmeasured.
- Capture actual desktop behavior, presentation pacing and RAM/VRAM under representative load; agree calibrated budgets.
- Review character feel/content direction on real assets and hardware; this batch is engine/sample capability, not finished flagship art.
- Public-release/Pax/legal gates remain later work; no release or spending is authorized by this batch.

## Continuous-work boundaries

During an active session, continue unblocked dependency-ordered implementation, tests, fixes, documentation and authorized private merges. Batch genuine product decisions/manual actions rather than repeatedly asking for permission already granted. Keep technical defaults explicit in ADRs. Do not invent measurements or acceptance, change billing, spend, publish, or overwrite another contributor's work. When a session ends or execution limits stop work, the saved branch/PR/status/checklist/manual list is the resume point; a chat reply does not create an indefinitely running unattended service.
