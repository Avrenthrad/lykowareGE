# ADR 0081 — Stumble, recovery and active ragdoll

Status: Proposed.
Related task/PR: Phase 2 checklist `02.13`, branch `claude/02.13-active-ragdoll`.
Supersedes / superseded by: none. Builds on ADR 0079 (ragdolls) and ADR 0080 (blending). Motors are this ID, not `02.12` (ADR 0073).

## Context

- **What exists:** a ragdoll with a pose read back (ADR 0079), and `Ragdoll::follow`, which pulls each body `1 - weight` of the way toward the animated pose by velocities each tick (ADR 0080).
- **The need:** `02.13` asks for a stumble, a recovery and motors that drive the ragdoll toward the animated pose.
- **Constraints:** bit-identical simulation at any frame rate (ADRs 0003, 0017); no Jolt type in a public header; physics keeps authority over contacts.

## Options

- **Motors as Jolt joint motors** on each `02.12a` joint. Needs new joint API and a second path to the same goal; the pull would then be split between the joint motor and `follow`.
- **Motors as `follow` at a fixed strength,** with a small state machine that drops the motors when the body is knocked too far and brings them back over time.

The second: it reuses ADR 0080's tested pull, keeps contacts in the world step, and adds no physics API.

## Decision

`ragdoll::ActiveProfile`, `ragdoll::ActiveState` and `ragdoll::ActiveRagdoll` in `ragdoll.hpp`.

- **Motors:** each tick, `ActiveRagdoll::tick` calls `follow` at weight `1 - strength`, so every body is pulled `strength` of the way to its animated place. `strength` is in (0, 1].
- **Measurement, every tick, before the motors act:**
  - *angle*: the worst joint of `read_pose` against the animated pose, in radians;
  - *offset*: how far the root bone's joint is from its animated place, in world metres.
- **States:**
  - `Animated` — motors on. Past `stumble_angle` or `stumble_offset` → `Stumbling`.
  - `Stumbling` — motors on. Within `recovered_angle` and `recovered_offset` for `recovered_seconds` → `Animated`.
  - Past `fall_angle` or `fall_offset` from either of those → `Fallen`.
  - `Fallen` — motors off (weight 1) for `lie_seconds`, then `Rising`.
  - `Rising` — weight goes in a straight line from 1 to `1 - strength` over `rise_seconds`, then `Stumbling`, which must still meet the recovered bounds before `Animated`.
- **"Recovered" in numbers:** by default, every joint within 0.08 rad of the animated pose and the root within 0.05 m, held for 0.25 s.
- **Off unless the profile asks:** `ragdoll::Desc::active` is optional. Absent, `ActiveRagdoll::create` refuses with "the profile does not ask for one". `Ragdoll::create` ignores the field, so a world's hash is the same with or without it. A character-profile JSON key for it is not part of this ADR; a loader that adds one fills `Desc::active`.
- **Refusals:** `create` refuses an absent profile, a strength outside (0, 1], a value not finite or negative, angles or offsets not ordered recovered < stumble < fall. `tick` refuses a step not finite and above 0, and whatever `follow` refuses, changing nothing.
- **Ownership:** the caller owns the controller and calls `tick` once per simulation tick in place of `follow`. `weight()` is the blend for `blended_pose`. The controller holds no world or ragdoll.
- **Replay:** the controller's inputs are the animated pose, the step and the world, so a replay that records what ADR 0080 records (the inputs, not the weight) reproduces its states. Its weight is derived, not an input.
- **Threading:** one thread at a time, as the world.

## Consequences

- Hit reactions are a shove plus a profile; the game does not script a weight curve.
- Motors are velocity pulls, so a strong profile is stiff and a weak one sags under gravity. The default strength in the test is 0.1 per tick at 60 Hz.
- A stumble here does not take a step. A stepping stumble would need the locomotion motor and is not part of `02.13`.

## Validation

`physics.ragdoll_active`, on the training mannequin standing on real physics, with a shove of fixed-seed direction (`std::mt19937`, seed 213) added to every body for 0.2 s after 1 s:

- **No shove:** stays `Animated`; motors hold the standing pose.
- **2 m/s shove:** leaves the animated pose past `stumble_offset` (0.15 m), stumbles without falling, and is back in `Animated` within the recovered bounds within 3 s.
- **4 m/s shove to torso and head:** bends past `fall_angle` (2.5 rad), falls, lies, rises, and ends in `Animated` within the recovered bounds.
- **Repeat:** the fall run repeats to the same hash and the same return tick.
- **Off:** no profile is refused; an unused profile leaves the world's hash unchanged.
- **Refusals:** each invalid profile value and each invalid tick above.
- **Deliberate breaks:** a run with no shove is caught as never leaving; motors at strength 0.001 are caught as never returning.
