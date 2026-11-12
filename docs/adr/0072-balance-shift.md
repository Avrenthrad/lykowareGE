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

- **`animation.balance_shift`** (new, straight-legged rig, real Jolt ground split by a step):
  - the left foot planted 8 cm down: the pelvis moves 4 cm toward it, eased in; the planted foot reaches (within 2 mm) and is never let go; the swing foot keeps its place (within 0.1 mm);
  - mirrored for the right foot; positive control without the key (no sideways move);
  - each limit: a 2 cm step gives 2 cm; a 1 cm bound gives 1 cm; hips 2 cm from the pelvis give 2 cm; a zero bound gives nothing;
  - short straight legs with a 9 cm shift still reach (the pelvis goes 8 mm lower for it);
  - an actor turned half round shifts along its own sideways axis;
  - easing out to exactly nothing in two-foot support, and with both feet lifted (feet within 1 mm of animated on straight legs); short legs easing out a wide shift let no plant go; a reset clears it;
  - flat-ground single support, two-foot support across a step, and standing still give exactly the same poses with and without the key;
  - 1,000 updates allocate nothing;
  - settings and profile refusals.
- **`animation.rig_acceptance`:** a sixth check (F) runs the shift on all eight rigs of ADR 0071; all pass.
- **`sandbox.imported_player`:** the real walking replay runs with the key on and reports `balance_frames`, with no failures; four bad profiles are refused.
- **`character.inspect_cli`:** reports `balance_shift` and `max_balance_shift`.
- **Deliberate breaks:** 20 of 20 real faults caught:
  - **controller (15):** never on; on without the key; away from the foot; ignoring the foot's depth, the bound, or the foot's position; along the world's axis instead of the actor's; snapping in; never easing out; not applied to the pose; feet not held while easing out; the reach drop ignoring the sideways move; following the pelvis instead of the foot's depth; the count not kept; a reset keeping the shift;
  - **settings, profiles and inspection (5):** the shift accepted without single support; any bound accepted; a profile bound accepted without the shift; a profile bound ignored; the inspection field missing.
  - **Found by the sweep and fixed:** the shift was first limited by the pelvis drop, which itself grows with the shift (a feedback); and a short straight leg could lose its plant to the sideways move. Tests for both, and for feet lifted during easing, were added.
  - **Two further mutations have no observable effect:** allowing the shift while standing (it is limited by a depth that is only set while walking), and applying the sideways reach drop only in single support (in two-foot support the pelvis eases up more slowly than the need shrinks).
- **Hosted Windows:** the second push failed to compile under MSVC (C4456, a local hiding an outer one in the controller); renamed.
- **Local results:** 87/87 pass in each of the Debug, ASan/UBSan and clang-tidy presets, with no skips.
