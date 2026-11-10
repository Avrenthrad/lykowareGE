# ADR 0070 — One planted foot lowers the pelvis to reach

Status: Proposed.
Related task/PR: Phase 2 checklist 02.11d, the one-foot part (branch `claude/single-support-pelvis`), based on `main` after #76.
Supersedes / superseded by: none. Extends ADR 0066's bounded pelvis and ADR 0067's walking anchors.

## Context

- **What exists:** with `foot_pelvis`, the pelvis drops (bounded, smoothed) toward the lower of two planted feet. It does nothing unless both feet have contact (ADR 0066, ADR 0067).
- **The visible problem:** while walking, most of a stride has one foot planted and the other swinging. When the planted foot lands on lower ground (a step down, a downhill stride), its leg cannot reach the anchor, so the plant is let go (ADR 0067's 2 mm reach rule) and the foot floats or slides.
- **Checklist:** 02.11d lists one-foot balance as open. The owner chose this part first, ahead of a balance lean and broad rig acceptance.

## Options

- **What the pelvis follows in single support:**
  - **nothing (as now);**
  - **the planted foot's depth.**

  The planted foot's depth, using the same bound (`foot_max_pelvis_drop`, at most a quarter of the shorter leg) and the same smoothing as two-foot support.
- **Smoothing versus reach:** a smoothed drop takes several frames to arrive, and on the first frames after a plant on lower ground the leg still cannot reach, so the plant would be let go before the pelvis catches up.
  - **raise the reach tolerance;**
  - **drop at once by what the leg needs, smoothing only beyond that.**

  The second was chosen: the pelvis is at least as low as the planted leg needs to reach its anchor (computed from the hip, the anchor and the leg's rest length), and no lower than the bound. Past the bound the plant is still let go, as before.
- **The swing foot:**
  - **let it drop with the pelvis,** which can push it into the ground;
  - **solve it back to its animated place.**

  It is solved back: the swing leg's two-bone IK targets where the animation put the foot, so only its knee bends more.
- **Opt-in:** a profile key, because it changes the walking pose and its feel needs the owner's review.

## Decision

Proposed:

- **Settings and profiles:**
  - `FootSettings::single_support`; profile key `foot_pelvis_single_support` (boolean, requires `foot_pelvis` and `foot_locking`);
  - `valid_foot_settings` refuses it without both;
  - `lyko inspect-character` reports `single_support`.
- **Sample controller (`samples/course/foot_placement.hpp`):**
  - applies only while walking with authored plant phases and exactly one foot in contact;
  - the pelvis follows that foot's depth, smoothed, and at once at least as low as its leg needs to reach, within the bound;
  - the swing leg is solved back to its animated foot position while the pelvis is lowered;
  - after a reach release in two-foot support, the remaining planted foot is handled as single support;
  - `single_support` (frames with a single-support drop) and `swing_held` counts join the foot diagnostics;
  - presentation only, as before.

## Contracts and compatibility

- **Additions only:** one settings field, one profile key, one inspection field, two diagnostics counts.
- **Unchanged without the opt-in;** with it, two-foot support and standing still are unchanged (tests compare poses bit for bit).
- **Not yet:**
  - a balance lean of the upper body (centre of mass over the support);
  - one-foot behaviour while standing still (only walking phases use it);
  - acceptance across varied rigs;
  - the owner's feel review.

## Consequences

- **Walking down steps and slopes keeps the planted foot on the ground** instead of letting it go, and the swing foot stays where it was animated.
- **The body dips more on uneven ground** while walking with the key on; whether that reads well is the owner's call in play.

## Verification

- **`animation.single_support`** (new, stance rig with straight legs, real Jolt ground split 8 cm):
  - the left foot planted on the lower side reaches it from the first frame (within 2 mm) and is never let go; the pelvis drops about 8 cm;
  - the swing foot keeps its animated place (within 0.1 mm);
  - positive control: with two-foot pelvis only, the plant is let go and the pelvis does not move;
  - mirrored for the right foot;
  - a 30 cm step: the plant is let go and the pelvis stays within 15 cm;
  - with bent legs, both feet planted and standing still give exactly the same poses with and without the key;
  - 1,000 updates allocate nothing;
  - settings and profile refusals.
- **`sandbox.imported_player`:** the real walking replay runs with the key on and reports the counts, with no failures; profiles with the key but no pelvis, no locking, or a non-boolean value are refused.
- **`character.inspect_cli`:** reports `single_support`.
- **Deliberate breaks:** the sweep runs next; results follow in the next commit.
- **Local results:** the full Debug suite passes (85/85); the other presets follow in the next commit.
