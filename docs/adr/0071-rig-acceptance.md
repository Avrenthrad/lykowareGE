# ADR 0071 — Which rig variations the foot features are verified on

Status: Proposed.
Related task/PR: Phase 2 checklist 02.11d, the rig-acceptance part (PR #78, branch `ccr-178f7225-atvbfx`; first pushed as `claude/rig-acceptance`), based on `main` after #77.
Supersedes / superseded by: none. Adds evidence for ADRs 0066–0070; changes none of them.

## Context

- **What exists:** stance placement, pelvis correction, normal alignment, heading lock and the one-foot pelvis (ADRs 0066–0070). Their tests use one stance fixture rig and the imported player.
- **The gap:** real character rigs differ in size and units, in proportions, in rest pose, in how joint frames are oriented, and in which extra joints surround the legs. A feature can be right on the fixture and wrong on any of these.
- **Checklist:** 02.11d lists acceptance across varied rigs as open. The owner chose it after the one-foot pelvis and ahead of the balance lean.

## Options

- **What to test against:**
  - **real third-party rigs,** which cannot be committed (licensing, ADR 0000);
  - **procedural rigs that vary one real difference at a time,** original and small.

  Procedural rigs were chosen. They say exactly which difference each one covers.
- **Where a failure is fixed:**
  - **in the engine,** when a rig shows a real fault;
  - **in the test,** when the test asked for something no rig can do.

  No rig showed an engine fault. One check asked too much and was changed (see the straight-leg note).

## Decision

Proposed:

- **A standing acceptance test,** `animation.rig_acceptance`, runs five foot checks on eight rigs on real Jolt ground.
- **The eight rigs** (`tests/support/rig_variants.hpp`):

  | Rig | What it varies |
  | --- | --- |
  | baseline | the stance fixture's proportions |
  | child | a 0.5-unit leg, narrow hips |
  | tall | a 1.1-unit leg |
  | long thighs | the thigh is 62% of the leg |
  | bent rest | knees bent 30° at rest |
  | bone axes | joint frames with +Y along the bone, as Blender exports them |
  | extras, bone axes, bent | a tilted root above the pelvis, a spine and toes |
  | centimetre units | a 90-unit leg drawn at 0.01 scale, with extras and bone axes |

- **The five checks, per rig:**
  - **A.** flat ground: the feet stay where animated (within 1 mm);
  - **B.** a step under one foot, pelvis correction on: each foot reaches its own ground (within 2 mm);
  - **C.** a 0.2 rad slope, normal alignment on: each foot's up axis meets the surface normal;
  - **D.** walking, both feet planted with heading lock, the body turned 30°: the feet keep their heading (within 0.1°) and no plant is let go;
  - **E.** walking, one foot planted on a step down with single support: it reaches (within 2 mm), is not let go, and the swing foot stays (within 0.1 mm).
- **The straight-leg reach note:** in check D the body rides 2% of a leg lower than rest.
  - A 30° turn moves each hip sideways. A leg that is already straight then cannot reach its plant, and the plant is let go by the 2 mm reach rule (ADR 0067).
  - That is the engine working as designed, and a real walk never plants on fully straight legs. The check gives the legs the slack a walk has; it does not change the reach rule.
- **What "verified" means here:** these eight variations, these five checks. It is not a claim about every rig.

## Contracts and compatibility

- **No engine, format, profile or tool change.** Tests only: one new test executable and one test-support header.
- **Rig limits stay as they were:** unit joint scales, a direct hip–knee–ankle chain, legs on separate branches below the pelvis (ADR 0066's calibration).
- **Not covered:**
  - non-uniform or non-unit joint scales (refused by calibration);
  - rigs that face another way than −Z or stand on another up axis than +Y;
  - twist or helper joints inside the leg chain (refused by calibration);
  - digitigrade or more-than-two-bone legs;
  - real imported third-party rigs beyond the imported player;
  - the owner's feel review (02.16).

## Consequences

- **A regression that only shows on a differently built rig is now caught in every test run,** on every platform.
- **New foot features have a place to prove rig independence:** add a check, and it runs on all eight rigs.
- **The balance lean remains open in 02.11d.**

## Verification

- **`animation.rig_acceptance`:** all eight rigs pass all five checks with no engine change.
- **Deliberate breaks:** 7 of 7 faults caught. Each one is a fault that the stance fixture alone could not show, or that the engine's own checks turn into a refusal:

  | Fault | Caught by | On rigs |
  | --- | --- | --- |
  | pelvis shift ignores the parent's rotation | B, E | extras; centimetre |
  | pelvis shift ignores the actor scale | B, E | centimetre |
  | single-support reach ignores the actor scale | E | centimetre |
  | heading reference taken as the joint's own −Z | D | the three bone-axes rigs |
  | heading turn ignores the parent's rotation | D | all eight |
  | foot up axis taken as the joint's own +Y | C | the three bone-axes rigs |
  | two-bone hip rotation ignores the parent's rotation | A–E (update refused) | the three bone-axes rigs |

  - **One weakness found and fixed:** the last fault made the controller refuse the update, and the test then read an empty pose and crashed without a message. Each check now reports a refused update with its error.
  - Two of the faults were first written in a form that did not compile (an unused variable under warnings-as-errors) and were re-run in a form that did.
- **Hosted Windows:** the first push failed to compile under MSVC (`size_t` to 16-bit joint index, warning C4267 as an error) in the rig builder. Fixed in the test support header; GCC and Clang do not warn there.
- **Local results:** see STATUS for the per-preset counts of this change.
