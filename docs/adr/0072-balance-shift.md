# ADR 0072 — The pelvis shifts over the planted foot on uneven ground

Status: Proposed.
Related task/PR: Phase 2 checklist 02.11d, the balance-lean part (branch `ccr-178f7225-atvbfx`), based on `main` after #78.
Supersedes / superseded by: none. Extends ADR 0070's one-foot pelvis.

## Context

- **What exists:** while walking with one foot planted on lower ground, the pelvis drops so that leg reaches (ADR 0070). The drop is straight down.
- **The visible problem:** a body that sinks onto one leg without moving over it looks unbalanced. A person stepping down shifts their hips over the supporting foot.
- **Checklist:** 02.11d lists full-body balance as its last open item.
- **Two choices were put to the owner and not answered** (the question was cut off by a session restart). The recommended answers are used here and are easy to change before this ADR is accepted.

## Options

- **What moves:**
  - **the pelvis slides sideways toward the planted foot,** carrying the upper body with it;
  - **a spine joint tilts,** which needs a new optional `spine` bone name in character profiles and does nothing on rigs without one;
  - **both.**

  The pelvis shift was chosen. It works on every rig the foot features already accept (ADR 0071) and changes no rig contract. A spine tilt can be added later on top of it.
- **When it applies:**
  - **on every one-foot stride,** which stacks on the sway already authored in a walk;
  - **only as far as the planted foot's ground is lower.**

  The second was chosen: flat-ground walking is untouched, and the shift grows with the step down.
- **Direction:**
  - **straight toward the planted foot,** which also pulls the body forward or back and fights the stride;
  - **sideways only** (the actor's left–right axis).

  Sideways only.
- **Opt-in:** a profile key, because it changes the walking pose and its feel needs the owner's review.

## Decision

Proposed:

- **Settings and profiles:**
  - `FootSettings::balance_shift` and `FootSettings::max_balance_shift` (metres, 0–0.1, default 0.04);
  - profile keys `foot_balance_shift` (boolean, requires `foot_pelvis_single_support`) and `foot_max_balance_shift` (number, requires `foot_balance_shift`);
  - `valid_foot_settings` refuses the shift without single support, and a bound outside 0–0.1;
  - `lyko inspect-character` reports `balance_shift` and `max_balance_shift`.
- **Sample controller (`samples/course/foot_placement.hpp`):**
  - in single support (ADR 0070), the pelvis also moves along the actor's left–right axis toward the planted foot;
  - the wanted shift is the smallest of: the planted foot's depth (the same depth the pelvis follows, within the pelvis bound), `foot_max_balance_shift`, and the sideways distance from pelvis to foot (so the pelvis never passes the foot);
  - the sideways move takes the hip out from above the foot, so the pelvis is lowered a little further, by what each leg in contact then needs to reach, within the pelvis bound;
  - it is smoothed like the drop, and eases back to nothing when single support ends;
  - while any shift remains, a foot without contact is solved back to its animated place, as the swing foot already is (on fully straight legs with no pelvis drop it can lift by under a millimetre while the shift eases out);
  - `balance_frames` joins the foot diagnostics, and `balance_shift()` reports the current signed shift;
  - presentation only, as before.

## Contracts and compatibility

- **Additions only:** two settings fields, two profile keys, two inspection fields, one diagnostics count.
- **Unchanged without the opt-in.** With it, poses are unchanged until single support meets lower ground: flat-ground walking, two-foot support and standing still give exactly the same poses.
- **Not yet:**
  - a spine or upper-body tilt;
  - any shift while standing still;
  - the owner's feel review.

## Consequences

- **Stepping down on one foot reads as balanced:** the hips come over the supporting foot as they sink.
- **The swing leg bends a little more** on those strides, because the pelvis moves away from it.
- **Whether the default 4 cm reads well** is the owner's call in play.

## Verification

Planned; results are recorded in STATUS when the work is done:

- a new `animation.balance_shift` test on real Jolt ground: the shift appears with a single-support drop, points at the planted foot (mirrored for the other side), respects each of its three limits, keeps the planted foot reaching and the swing foot in place, eases out, changes nothing on flat ground or without the key, allocates nothing, and refuses bad settings;
- a sixth check in `animation.rig_acceptance`, on all eight rigs;
- the imported-player walk with the key on;
- deliberate breaks;
- the Debug, ASan/UBSan and clang-tidy presets, and the five hosted jobs.
