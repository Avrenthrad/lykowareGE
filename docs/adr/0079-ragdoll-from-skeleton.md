# ADR 0079 — Ragdoll from a skeleton, pose read back

Status: Proposed.
Related task/PR: Phase 2 checklist 02.12, third part (`02.12c`), branch `ccr-df6f86f8-xesgcx-0212c`, on top of `02.12b` (ADR 0078).
Supersedes / superseded by: none. Uses the joints of ADR 0073 and the masses of ADR 0078.

## Context

- **What exists:**
  - physics bodies with masses and pose drives (ADR 0078);
  - fixed, hinge and swing-twist joints with limits (ADR 0073);
  - skeletons and local poses (`animation.hpp`).
- **The need:** `02.12c` asks for a ragdoll built from a skeleton, one body per bone with joints and limits, and a pose read back from the bodies. `02.12d` will blend that pose with animation.
- **Constraints:**
  - no Jolt type in a header;
  - a skeleton may have joints that should not get a body (fingers, twist bones, a root above the pelvis);
  - rigs come in metres and in centimetres (`animation.rig_acceptance`).

## Options

- **Where a body runs:**
  - **from a joint to its child joint,** which fails for leaf joints and branching joints;
  - **along a segment the caller gives in the joint's own space.**

  Segment. The caller names the joint, the segment's two ends, a radius and a mass.
- **Joints without a body:**
  - **refuse the skeleton;**
  - **let them keep their animated local pose,** so they ride on the nearest ancestor that has a body.

  The second.
- **Who owns the bodies:**
  - **the ragdoll,** which then has to outlive or watch the world;
  - **the world,** as with every other body.

  The world.

## Decision

Proposed: `lykoware::ragdoll::Ragdoll` in `ragdoll.hpp`.

- **`Ragdoll::create(world, skeleton, pose, placement, desc, error)`** makes the bodies and joints at the pose.
  - `Placement` puts model space in the world: position, rotation, and a uniform scale (world metres per model unit; 0.01 for a centimetre rig).
  - Each `Bone` has a joint, a segment (`start` and `end` in the joint's space, model units), a capsule `radius` (metres) and a `mass` (kilograms).
  - A bone also has a `limit`: the `JointKind` to the body of its nearest ancestor joint that has one, anchored at the joint's origin. Angles are measured from the pose the ragdoll is made in. A hinge turns about `hinge_axis` in model space. A swing-twist joint twists about the bone's segment. Exactly one bone, the root bone, has no ancestor bone and no joint.
- **Limits:** limits are the joint kinds of ADR 0073, with their ranges. A reversed limit (`min_angle` above `max_angle`, `twist_min` above `twist_max`) is refused and named as reversed.
- **Refused** (nullopt, error filled, nothing left in the world) for:
  - an invalid skeleton or a pose of the wrong size;
  - a non-finite placement, or a scale that is not above 0;
  - no bones, a joint out of range, or two bones on one joint;
  - a joint scale other than 1 (scale the placement instead);
  - a segment under 0.1 mm;
  - a radius or mass that is not finite and above 0;
  - a reversed limit or a zero hinge axis;
  - not exactly one root bone;
  - any refusal from the world. Bodies already made are removed.
- **A missing bone** (a joint with no body) keeps its animated local pose under its parent.
- **`read_pose(world, animated, out)`:**
  - Each joint with a body takes its rotation from that body.
  - The root bone's joint also takes its place from its body.
  - Other joints keep their animated translation, because bones do not stretch.
  - Joints without a body keep `animated`.
  - It returns false, leaving `out` alone, after `destroy()`, when any body is gone, or when `animated` has the wrong size.
- **Lifetime follows the world:**
  - The bodies and joints are the world's.
  - `destroy(world)` removes them.
  - Destroying the world removes them too.
  - A `Ragdoll` that is never destroyed leaves its bodies in the world.
  - The ragdoll copies what it needs from the skeleton and holds no pointer to it.
- **Threading:** one thread at a time, with the world's own rule.

## Consequences

- `02.12d` can drive each body toward the animated pose with `drive_to_pose` (ADR 0078) and blend the read pose with the animated one.
- Body shapes are capsules only. A box or a convex hull per bone can be added later without changing the call.

## Validation

`physics.ragdoll` covers the following:

- **Round trip without a step:**
  - the training mannequin, at a walk pose, placed turned and moved;
  - all eight rigs of `rig_variants::all()`, including the centimetre rig, with one knee and one hip moved from rest.
  - Rotations must match within 1e-5 rad, and the root's place within 1e-4 m in world units. Locally the worst is about 2e-7 rad.
- **Joints and contact:** the mannequin falls onto ground for three seconds and stays finite, on the ground and not through it.
- **Missing bone:** a head without a bone keeps its animated pose exactly.
- **Lifetime:**
  - a ragdoll with a body removed does not read back;
  - `destroy` removes every body and joint;
  - a destroyed ragdoll does not read back.
- **Refusals,** each leaving the world empty:
  - a zero-length bone made after four others;
  - reversed twist and hinge limits;
  - a joint out of range, or two bones on one joint;
  - two root bones;
  - a zero mass or a non-finite radius;
  - no bones, or a wrong-size pose;
  - a scaled joint, or a placement scale of 0.
- **Deliberate break:** turning one arm's body away moves the arm in the read pose by about 0.09 rad, far over the round-trip bound, so the bound can fail.
