# ADR 0078 — Body mass, and moving a body to a pose

Status: Proposed.
Related task/PR: Phase 2 checklist 02.12, second part (`02.12b`), branch `ccr-df6f86f8-xesgcx-0212b`, based on `main` after #97.
Supersedes / superseded by: none. Extends the physics API of ADR 0020 and ADR 0073.

## Context

- **What exists:** `physics::World` has bodies, joints (ADR 0073), characters, ray casts and a state hash. Every dynamic body weighs what its shape holds at 1000 kg/m³, and the only way to move one is `set_velocity`.
- **The need:** a ragdoll (`02.12c`) needs bones of a person's weight, not of water-filled boxes. Blending animation into physics (`02.12d`) needs to carry a bone's body to the animated pose each step without teleporting it through what it touches.
- **Constraints:** no Jolt type in the header (ADR 0020); bit-identical simulations (ADR 0003); worlds that do not use the new calls keep their hashes.

## Options

- **How to give a mass:**
  - **a density,** which a caller must turn into kilograms from the shape's volume;
  - **a mass in kilograms,** what animators and the character profile already speak.

  Mass. The inertia is the shape's, scaled to that mass.
- **How to move a body to a pose:**
  - **teleport it** (set the position): it passes through walls and starts every contact fresh;
  - **set velocities that arrive in one step,** as Jolt moves kinematic bodies, so contacts and joints still act.

  Velocities. For a dynamic body the step's gravity and damping are undone first, so with nothing touching it the body ends the step at the pose.

## Decision

Proposed:

- `BodyDesc::mass`, kilograms. `0` (the default) keeps the shape's mass at 1000 kg/m³ and leaves body creation exactly as before. Above 0 replaces it, with the shape's inertia scaled to match. Refused (nullopt, error filled) when negative or non-finite, or when given for a static or kinematic body.
- `World::mass(BodyId)`: the mass in kilograms of a dynamic body; nullopt for an invalid, destroyed or foreign handle or a static or kinematic body.
- `World::drive_to_pose(BodyId, DVec3 position, Quat rotation, float seconds)`: sets the body's velocities so that the next `step(seconds)` ends at the pose. Works on dynamic and kinematic bodies and wakes them. Returns false and changes nothing for an invalid handle, a static body, a non-finite position, a zero or non-finite rotation, or `seconds` not above 0.
- **Units:** metres, radians through the quaternion, seconds, kilograms.
- **Limits:** contacts and joints still act on a dynamic body, so it can stop short. Jolt caps speed at 500 m/s and spin near 47 rad/s, so a pose that far in one step is reached over several. A pose that jumps further than a body's own size in one step can pass through a thin wall; drive toward moving poses every step, not across the room in one.
- **Threading:** one thread at a time, as `World` today.
- **Hashes:** worlds that do not set `mass` or call `drive_to_pose` are bit-identical to before; `physics.world`'s golden hash shows it.

## Consequences

- `02.12c` can build a ragdoll from a skeleton with real bone masses, and `02.12d` can blend by driving bodies to the animated pose.
- Mass is not in `state_hash()` directly; a different mass changes the motion, and the motion is hashed.

## Validation

- `physics.body_pose`: masses kept, defaulted and refused; a heavier sphere wins a head-on collision; dynamic and kinematic bodies reach a pose in one step under gravity (within 0.1 mm and 1 mrad); a body driven every step follows a moving pose for two seconds; a wall stops a driven body; refusals change nothing; foreign and destroyed handles refused; replays are bit-identical.
- Deliberate break: the naive drive (velocity = distance / time) misses by about 3 mm in one 60 Hz step, so the reach check can fail.
- `physics.world` (golden hash) and `physics.joints` still pass.
