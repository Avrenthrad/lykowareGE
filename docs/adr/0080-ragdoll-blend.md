# ADR 0080 — Blending animation and physics on a ragdoll

Status: Proposed.
Related task/PR: Phase 2 checklist 02.12, last part (`02.12d`), branch `ccr-df6f86f8-xesgcx-0212d`.
Supersedes / superseded by: none. Builds on ADR 0078 (pose drives) and ADR 0079 (ragdolls).

## Context

- **What exists:**
  - A ragdoll whose bodies a world simulates, and whose pose can be read back (ADR 0079).
  - A call that moves a body to a pose through velocities (ADR 0078).
- **The need:** `02.12d` asks for a blend between animation and physics, in the sample and in replay. For example, a character goes limp when hit, then partly recovers.
- **Constraints:**
  - Simulations stay bit-identical at any frame rate (ADRs 0003 and 0017).
  - Physics keeps authority over contacts.

## Options

- **What the weight blends:**
  - **only the drawn pose.** The bodies would then run free whatever the weight, so weight 0 would leave the ragdoll lying wherever it fell.
  - **both the bodies and the drawn pose.**

  Both:
  - The bodies are pulled toward the animated pose by `1 - weight`.
  - The drawn pose is the animated pose blended `weight` of the way to what the bodies hold.
- **How the bodies are pulled:**
  - **by teleporting them,** which tunnels and ignores contacts.
  - **by velocities** (`drive_to_pose`), so the step still resolves contacts and joints.

  Velocities.

## Decision

Proposed: `Ragdoll::follow`, `Ragdoll::blended_pose` and `Ragdoll::set_placement` in `ragdoll.hpp`.

- **The weight:**
  - **0 is the animated pose.** Every body is driven onto where the animated pose puts it, and the drawn pose is the animated one.
  - **1 is the physics pose.** `follow` leaves the bodies to the world, and the drawn pose is what they hold.
  - **A value between** pulls each body `1 - weight` of the way from where it is toward its animated place. Positions move in a straight line, rotations along the shorter arc. The drawn pose is `animation::blend(animated, physics, weight)`.
- **Who owns the weight:** the caller, such as the game or a sample, passes it every tick. The ragdoll stores none.
- **Contacts:** physics still resolves contacts and joint limits in `World::step`. A pulled body can stop short of its animated place, for example against the ground or another body that shares no joint with it.
- **Rigid bones:** joints are points that do not move inside their bodies. A clip that slides a joint's translation therefore cannot be followed exactly. The sample holds every joint but the root at its rest translation.
- **Refusals:** `follow` and `blended_pose` return false and change nothing in these cases:
  - a weight outside 0..1, or not finite;
  - a step length (`seconds`) that is not finite or not above 0;
  - an animated pose of the wrong size;
  - a ragdoll that has been destroyed, or has a body gone.
- **Replay records the weight:** one float per tick. The sample path (`ragdoll_blend::run_sample`) records each tick's weight. The headless path (`ragdoll_blend::replay`) runs those weights with no frames or clock. A recorded weight outside 0..1 refuses the replay.
- **Threading:** one thread at a time, as the world.

## Consequences

- Hit reactions and recoveries can be written as a weight curve, and `02.13` can add motors on top.
- The weight is part of the simulation's input. A tool that records a session must record it, as it records player input.

## Validation

`physics.ragdoll_blend` runs the training mannequin, walking in place on real physics, through a scripted weight:
- 0 for 1 s;
- up to 1 over 0.5 s;
- 1 for 1 s;
- back to 0.3.

It checks:
- **Frame rates:** the sample path at 30, 60 and 144 Hz frames, through `FixedStep` at 60 ticks a second, matches a headless replay of its recorded weights bit for bit. All three rates also agree tick for tick.
- **Weight 0:** the bodies stay within 0.02 rad of the walk (locally 0.009), and the drawn pose is the animated pose.
- **Weight 1:** the ragdoll falls (torso from 1.34 m to 0.15 m).
- **Weight 0.3:** the drawn pose is the blend of animated and physics.
- **Refusals:**
  - weights −0.1, 1.1 and NaN are refused and leave the world unchanged;
  - `blended_pose` refuses a weight of 2;
  - a replay containing 1.5 is refused.
- **Deliberate break:** changing one recorded weight by 0.01 changes the replay's hash.
