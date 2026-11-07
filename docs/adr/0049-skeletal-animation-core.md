# ADR 0049 — Skeletal animation core: skeletons, clips, poses and skinning

Status: Proposed.
Related task/PR: Phase 2 PR 4 (branch `claude/animation-core`), stacked on drawing between steps ([ADR 0048](0048-drawing-between-steps.md)).
Supersedes / superseded by: none. Uses the maths module ([ADR 0024](0024-math.md)).

## Context

- **What Phase 2 asks for:** "foot IK, locomotion transitions, active ragdoll stumble/recovery and animation/physics blending". All of them need a skeleton that can be posed.
- **What exists:** the engine draws rigid meshes only, and its glTF reader ignores skins and animations (ADR 0035).
- **Agreed direction:** "skeletal animation next", the owner's "Continue" (2026-11-06) to the recommendation.
- **Plan:** split into small PRs:
  1. this one: the data and the maths, with no file reading or drawing;
  2. reading glTF skins and animations into these types;
  3. drawing skinned meshes;
  4. an animated character in the sandbox, with walk/run blending.

## Options

- **Whose code:**
  - **write our own;**
  - **vendor a library** (ozz-animation, MIT).

  Our own was chosen:
  - The maths is small: key search, interpolation and a parent-first walk.
  - It matches the owner's choices for glTF, the ECS and the maths module ("write our own").
  - It keeps the engine free of another dependency. ozz's compression and SIMD runtime can be revisited if profiling ever asks for them.
- **The clip model:**
  - **glTF's own:** keyed translation, rotation and scale per joint, with step, linear and cubic-spline interpolation;
  - **a resampled, fixed-rate format.**

  glTF's was chosen. The reader can fill clips as they are, with nothing lost. Resampling can come later as an import step if it pays.
- **Rotation interpolation:** slerp, as glTF specifies, with nlerp only where two keys are nearly equal. Blending two poses uses nlerp, which is the usual choice for weights and keeps blends cheap.

## Decision

Proposed: `lykoware/animation.hpp` (`lykoware::animation`):

- **`Skeleton`:** joints in parent-first order.
  - Each joint has a name, an optional parent, a rest `JointPose` (translation, rotation, scale) and an inverse bind matrix.
  - At most 256 joints.
- **`Clip`:** channels. Each keys one joint's translation, rotation or scale.
  - Interpolation is step, linear or cubic spline.
  - Values are laid out as glTF does: one per key, or in-tangent, value and out-tangent per key for a cubic spline.
- **Validation:** `valid_skeleton` and `valid_clip` refuse what would sample wrongly, each with a reason:
  - parents after children;
  - non-finite numbers;
  - non-unit rest rotations and zero scales;
  - times not strictly increasing;
  - value counts that do not match;
  - joints outside the skeleton;
  - a joint's target keyed twice.
- **`sample(clip, seconds, loop, pose)`:** overwrites only what the clip keys.
  - Time wraps round the duration when looping, else it is clamped.
  - Before the first key a channel holds its first value; after the last, its last.
  - A non-finite time counts as 0.
- **`blend(a, b, weight, out)`:** mixes two poses for transitions.
- **`global_transforms`, `skinning_matrices` and `skin_point`:** global is parent times own. Skinning is global times inverse bind. A vertex is the weighted sum over up to four joints.

## Contracts and compatibility

- **New:**
  - `lykoware/animation.hpp`;
  - `engine/src/animation.cpp`;
  - the `animation.pose` test.
- **Unchanged:** existing APIs.
- **Threading:** plain data and pure functions.
- **Repeatability:** slerp calls `acos` and `sin`, whose last bit may differ between C libraries. Poses are for drawing and for gameplay that tolerates that; this is the maths module's rule (ADR 0024).
- **Not yet:**
  - reading glTF skins and animations;
  - skinned drawing;
  - morph targets;
  - additive clips and masks;
  - clip compression;
  - IK.

## Consequences

- **Foundation for Phase 2:** the later PRs (reading, drawing, locomotion, foot IK, ragdoll blending) build on one tested set of types and functions.

## Verification

- **`animation.pose`** (new; no files, window or GPU), against values worked out by hand:
  - **Skinning a shoulder–elbow–hand arm:**
    - at rest, every vertex stays put;
    - a quarter turn of the shoulder lifts the hand to (0, 2, 0);
    - bending the elbow too folds the forearm back to (−0.5, 1, 0);
    - a vertex weighted half and half lies between, at (−0.25, 1.25, 0);
    - a parent's scale carries down the chain;
    - a joint index past the end counts as weight 0.
  - **Step and linear:**
    - step holds a key until the next;
    - linear is straight between keys;
    - looping wraps both ways;
    - outside the keys a channel holds its ends;
    - a non-finite time counts as 0;
    - a clip lasts until its longest channel ends;
    - unkeyed parts keep their pose.
  - **Rotation:**
    - halfway from 0° to 90° is 45°, and a quarter of the way is 22.5° (constant speed, which nlerp would miss);
    - a key with the opposite sign still takes the shorter way;
    - 0° to 170° gives 85° halfway;
    - results are normalised.
  - **Cubic spline:** flat tangents give smoothstep (0.15625 a quarter of the way). Out- and in-tangents scaled by the key interval match glTF's Hermite formula (0.71875 and 0.1875).
  - **Blend:** straight-line translation and scale, the rotation halfway along the shorter way, the weight clamped, and the output may be one of the inputs.
  - **Refusals:** every case listed under Decision.
- **Deliberate breaks:** 21 of 21 caught:
  - nlerp instead of slerp;
  - slerp taking the long way;
  - cubic tangents not scaled by the interval;
  - in- and out-tangents swapped;
  - a cubic key's value read as a tangent;
  - step taking the next key;
  - negative loop times not wrapped;
  - a non-finite time kept;
  - the duration of the first channel only;
  - parent and child multiplied the wrong way round;
  - no inverse bind;
  - the inverse bind on the wrong side;
  - weights ignored;
  - blending the long way;
  - an unclamped blend weight;
  - sampled rotations not normalised;
  - a self-parent accepted;
  - repeated key times accepted;
  - cubic value counts not checked;
  - duplicate targets accepted;
  - non-unit rotations accepted.
- **Local results:** 60/60 pass in the Debug, ASan/UBSan and clang-tidy presets, with no skips. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion` sweep finds nothing in the new code.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
