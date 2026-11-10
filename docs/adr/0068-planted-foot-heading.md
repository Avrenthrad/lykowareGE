# ADR 0068 — Planted feet keep their heading

Status: Proposed.
Related task/PR: Phase 2 checklist 02.11c, the "full foot heading" part (branch `claude/foot-heading-lock`), based on `main` after #74 (`33c9c9c`).
Supersedes / superseded by: none. Extends ADR 0067's foot-plant anchors; uses ADR 0066's surface normals.

## Context

- **What ADR 0067 holds:** a planted foot's position and surface normal, in the support's frame.
- **What it leaves out:** its heading, the way the toes point. ADR 0067 and `docs/WALKING_FOOT_PLANTS.md` list "full foot-yaw locking" as separate work.
- **The visible problem:** when the body turns while a foot is planted (turning on the spot, or a curved walk), the foot swivels with the animation, so it slides on the ground about its own axis.
- **Checklist:** 02.11c stays partial until this and moving-support render interpolation are done.

## Options

- **What the heading is measured from:**
  - **the foot's real toe direction,** which needs a toe joint or a per-rig calibration;
  - **a fixed reference axis on the foot.**

  The reference axis was chosen: the foot's model −Z, which at rest is horizontal and at right angles to its up axis (model +Y, the axis ADR 0066 aligns). Only the twist about the ground matters for keeping a foot still, and any horizontal reference measures it. No toe joint or extra calibration is needed. The profile's visual yaw is a rotation about +Y, so it keeps −Z horizontal.
- **What the foot turns about:**
  - **the foot's own up axis;**
  - **the support's surface normal.**

  The surface normal was chosen. A first version turned about the foot's up axis and missed the held heading by 0.4–1.4° on flat ground, because leg IK leaves the foot slightly tilted. Turning about the normal makes the reference axis, laid in the support plane, match the held heading exactly. With normal alignment on, the foot's up axis is that normal, so the alignment is kept.
- **Large turns:**
  - **release the plant;**
  - **clamp the twist.**

  Clamping was chosen: at most `foot_max_heading_degrees` (default 60). The foot then follows the body by the excess, and a fresh step re-plants with the new heading.

## Decision

Proposed:

- **`FootPlant` (engine):**
  - a new `update` overload takes the animated foot's heading (finite and non-zero; otherwise refused, with state and output kept);
  - on a fresh plant, the heading's part in the support plane is kept in support coordinates;
  - while held, `FootPlantResult::heading` carries it through the support's translation and rotation;
  - a heading along the normal holds none;
  - lift, release (too far, too steep, support lost) and reset drop it;
  - the old overload is unchanged and holds no heading;
  - successful calls still allocate nothing.
- **Profiles:**
  - `foot_heading_lock` (boolean, requires `foot_locking`);
  - `foot_max_heading_degrees` (0–90, requires `foot_heading_lock`);
  - `valid_foot_settings` refuses a heading lock without foot locking;
  - `lyko inspect-character` reports `heading` and `max_heading_degrees`.
- **Sample controller (`samples/course/foot_placement.hpp`):**
  - while walking, it passes each foot's animated reference axis to its plant;
  - after IK and normal alignment, each planted foot with a held heading is rotated about the support normal toward it, by at most the bound;
  - `headed` and `heading_clamped` counts appear in the sandbox's foot diagnostics;
  - presentation only, as before.

## Contracts and compatibility

- **Additions only:**
  - `FootPlant::update` overload;
  - `FootPlantResult::heading`;
  - `FootSettings::heading` and `max_heading_degrees`;
  - two profile keys and two report fields.
- **Unchanged without the opt-in:** profiles and the existing overload behave exactly as before.
- **Repeatability:** the twist uses `atan2`, `sin` and `cos`, so like the rest of the animation path it is for presentation, not cross-platform replay.
- **Not yet:**
  - moving-support render interpolation (the other half of 02.11c, next);
  - balance and full-body constraints (02.11d);
  - a real toe joint;
  - owner feel review.

## Consequences

- **Planted feet stay put:** a turning character's planted feet hold their heading on the ground instead of swivelling. Past the bound, they follow by the excess.

## Verification

- **`animation.foot_heading`** (new):
  - **engine:**
    - a fresh plant holds the animated heading and ignores later ones;
    - lift drops it;
    - tilted headings are flattened onto the surface;
    - non-finite and zero headings are refused with output and state kept;
    - a heading along the normal holds none;
    - the old overload holds none;
    - release drops it;
    - on a support turning at 1 rad/s the held heading turns with it (within 1e-4);
    - on a tilted support it lies in the surface plane;
  - **controller, real Jolt floor and the stance fixture:**
    - with the body turned 30° over planted feet, both feet keep their heading (within 0.05°);
    - without the lock the foot turns with the body (about 30°, the positive control);
    - a 100° turn is clamped at 60° (about 40° remain), counted;
    - a lift and replant takes the new heading;
    - with normal alignment on a slope, the foot's up axis stays on the surface normal (within 2e-3);
    - 1,000 heading-locked updates allocate nothing;
  - **settings and profiles:** the bounds, the foot-locking requirement and the profile keys.
- **`sandbox.imported_player`:** the real imported-player walking replay with `foot_heading_lock` runs with `headed` above 0 and no failures. Profiles with a heading lock but no locking, a bound of 91, or a bound without the lock are refused.
- **`character.inspect_cli`:** reports the new policy fields.
- **Deliberate breaks:** 15 of 15 real faults caught:
  - **engine:** no projection onto the surface; the heading kept in world coordinates (first missed; the test now plants on an already-turned support); the held heading not carried by the support; the animated heading followed while held; non-finite headings accepted; a heading along the normal kept;
  - **controller:** the heading not applied; the twist reversed; the twist unbounded; turning about model up instead of the support normal; the parent rotation ignored; the wrong reference axis; the stats not counted;
  - **settings:** a heading lock without locking accepted; the bound unchecked.

  One further mutation (not clearing the heading in `reset()`) has no observable effect, because a fresh plant resets the heading first.
- **Local results:** 83/83 pass in each of the Debug, ASan/UBSan and clang-tidy presets, with no skips. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion` sweep finds nothing in the new code, after two float-to-double promotions in the sample's new direction helpers were made explicit.
