# ADR 0071 — Foot features are verified across varied rigs

Status: Proposed.
Related task/PR: Phase 2 checklist 02.11d, the rig acceptance part (branch `claude/rig-acceptance`), based on `main` after #77.
Supersedes / superseded by: none. Verifies ADRs 0065–0070 on more than one rig.

## Context

- **What exists:** foot placement, normal alignment, pelvis correction, walking plants, heading lock, drawn supports and single support (ADRs 0065–0070).
- **The gap:** nearly every test uses one rig, the stance fixture: 1.6-unit straight legs, identity joint frames, metre units, no joints above the pelvis or below the ankles. Real rigs differ in all of those, and a fault that only shows with rotated frames, a scaled actor or a rotated parent would pass every existing test.
- **Checklist:** 02.11d lists acceptance across varied rigs as open. The owner chose it next.

## Options

- **Where the rigs come from:**
  - **downloaded characters,** which bring licence and repository-size questions;
  - **original procedural rigs built in the test.**

  Procedural rigs were chosen: no assets, no licences, and each variation is one named parameter.
- **What to vary:** the things character rigs really vary — size, thigh/shin proportion, hip width, knees bent at rest, joint frames that point along the bone (as Blender exports them), extra joints (a tilted root above the pelvis, a spine, toes), and centimetre units with a 0.01 actor scale.
- **What to check:** one check per foot feature, on real Jolt ground, with tolerances fixed in the test and step heights scaled to the leg.

## Decision

Proposed:

- **`tests/support/rig_variants.hpp`:** builds a rig from a `Spec` (leg length, thigh share, hip half-width, rest knee bend, bone-aligned frames, extras, actor scale) and lists eight: baseline, child, tall, long thighs, bent rest, bone axes, extras with bone axes and bent knees, and centimetre units.
- **`animation.rig_acceptance`:** for every rig,
  - A. flat ground: the feet stay where animated;
  - B. a step with pelvis correction: each foot reaches its own ground, and the folded knee points forward;
  - C. a 0.2 rad slope with normal alignment: each foot's up axis meets the surface normal;
  - D. walking plants with heading lock and a 30 degree body turn: the feet keep their heading and no plant is let go;
  - E. single support on a step down: the planted foot reaches, the swing foot stays.
- **No engine or sample change was needed:** all eight rigs passed as written.

## Contracts and compatibility

- **Tests only.** No API, format or behaviour change.
- **A rig is supported when** it has the seven leg roles, calibrates (`calibrate_feet`), faces -Z in model space and is placed with a uniform actor scale. Joint frames, proportions, units and extra joints are free.
- **Straight legs have no slack:** with fully straight legs, a body turn moves the hips sideways and a planted foot can go out of reach, so the plant is correctly let go (ADR 0067's reach rule). Check D lowers the body 2% of a leg, as a walk cycle does. This is expected behaviour, not a fault.
- **Not covered:** non-uniform scale, rigs facing another axis, twist or roll bones in the leg chain, digitigrade or more-than-two-bone legs, real downloaded characters, and the owner's feel review.

## Consequences

- **New foot work has a wider net:** a fault tied to frames, scale, units or parents now fails a named rig.
- **Adding a rig shape is one line** in `rig_variants::all()`.

## Verification

- **`animation.rig_acceptance`:** eight rigs, five checks each, all pass.
- **Deliberate breaks:** 7 of 7 caught, each in the sample controller:
  - the pelvis move ignoring the parent's rotation;
  - the pelvis move ignoring the actor scale;
  - the heading reference not calibrated from the rest pose;
  - the heading twist ignoring the parent's rotation;
  - the single-support reach ignoring the actor scale;
  - normal alignment using the wrong rest axis;
  - the knee pole behind the leg (first missed; check B now requires the folded knee to point forward).

  Four of these (parent rotation twice, actor scale twice) fail only on the new rigs with extras or centimetre units, not on the baseline.
- **Local results:** 86/86 pass in each of the Debug, ASan/UBSan and clang-tidy presets, with no skips.
