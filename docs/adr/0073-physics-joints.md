# ADR 0073 — Joints between physics bodies

Status: Proposed.
Related task/PR: Phase 2 checklist 02.12, first part (branch `ccr-178f7225-atvbfx`), based on `main` after #80.
Supersedes / superseded by: none. Extends the physics API of ADR 0020.

## Context

- **What exists:** `physics::World` has bodies, characters, ray casts and a state hash (ADR 0020). Bodies cannot be connected to each other.
- **The need:** 02.12 asks for ragdolls. A ragdoll is a set of bodies, one per bone, held together at the joints with limits (a knee bends one way, a shoulder swings in a cone).
- **Constraint:** no Jolt type may appear in the public API (ADR 0020), and simulations must stay bit-identical (ADR 0003).

## Options

- **Which joints:**
  - **every Jolt constraint,** most of which nothing needs yet;
  - **the three a ragdoll needs:** fixed, hinge (knee, elbow) and swing-twist (hip, shoulder, spine, neck).

  The three. More can be added the same way when something needs them.
- **How a joint is described:**
  - **in each body's own space,** which callers must compute;
  - **in world space, as the bodies are now.**

  World space: an anchor point and one axis. The pose the bodies have when the joint is made is its zero.
- **Collision between the two jointed bodies:**
  - **always on,** which makes overlapping bone shapes fight at every joint;
  - **always off;**
  - **off by default, with a flag.**

  Off by default, with `collide` to turn it back on.
- **When a body is destroyed:**
  - **refuse while it has joints;**
  - **destroy its joints with it.**

  Its joints go with it, as a world's bodies go with the world.

## Decision

Proposed:

- **Types (`lykoware/physics.hpp`):**
  - `JointId`, a handle with the same rules as `BodyId`;
  - `FixedJoint`, `HingeJoint{min_angle, max_angle}`, `SwingTwistJoint{swing_half_angle, twist_min, twist_max}`, joined as `JointKind`;
  - `JointDesc{parent, child, kind, anchor, axis, collide}`;
  - `WorldConfig::max_joints` (default 4096).
- **Functions on `World`:** `create_joint`, `destroy_joint`, `is_valid(JointId)`, `joint_count`.
- **Rules:**
  - two different live bodies of the same world, at least one dynamic;
  - angles in radians, each within its stated range; the axis any non-zero length (unused by a fixed joint);
  - creating or removing a joint wakes both bodies;
  - destroying a body destroys its joints;
  - `state_hash()` also covers which bodies each joint holds and its kind.
- **Limits are firm, not rigid:** struck at speed, a limit can be overshot for a moment (about 0.1 rad in the tests) and settles within a few hundredths of a radian. That is the solver's behaviour and is recorded, not hidden.

## Contracts and compatibility

- **Additions only.** Worlds without joints behave and hash exactly as before.
- **Ownership:** the world owns its joints. Handles of destroyed joints never become valid again.
- **Threading:** as the rest of `World`: one thread at a time.
- **Failure:** `create_joint` returns `nullopt` and fills `error`; nothing throws for bad input.
- **Not yet (later parts of 02.12):**
  - body mass and density, and moving a body to a pose;
  - building a ragdoll from a skeleton, and reading a pose back from it;
  - blending between animation and physics;
  - motors (active ragdoll, 02.13);
  - joints in scene files and the ECS link.

## Consequences

- **Ragdolls can be built on the owned API** without any caller seeing Jolt.
- **Doors, chains and hanging props** become possible with the same three joints.
- **Every non-colliding joint adds a pair to a lookup** that the collision step reads; it is a sorted list, searched in logarithmic time.

## Verification

Planned; results are recorded in STATUS when the work is done:

- a new `physics.joints` test through the public API only: each joint kind holds and limits as described, with positive controls; the collide flag; two joints on one pair; a third body still collides; lifetime (destroying a body, removing a joint, handle reuse, the joint limit); every refusal; bit-identical repeats and hash coverage;
- deliberate breaks;
- the Debug, ASan/UBSan and clang-tidy presets, and the five hosted jobs.
