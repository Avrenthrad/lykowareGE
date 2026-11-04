# ADR 0020 — The Lykoware physics API

Status: Proposed
Related task/PR: physics spike, PR 2 (branch `claude/physics-api`), after spike PR 1 ([#21](https://github.com/Avrenthrad/lykowareGE/pull/21)) merged.
Numbered 0020 because a concurrent documentation proposal ([#22](https://github.com/Avrenthrad/lykowareGE/pull/22)) claimed 0019 first.
Supersedes / superseded by: none. Builds on [ADR 0003](0003-physics.md), [ADR 0017](0017-clocks-fixed-step.md) and [ADR 0018](0018-jolt-integration.md).

## Context

ADR 0003 requires that no third-party physics type crosses into game or tool code. It also says Lykoware owns stable IDs, lifecycle, collision filtering, queries, world stepping, units and failure behaviour. Spike PR 1 proved that Jolt builds and is deterministic, but its tests call Jolt directly. Game code needs a small, owned API with defined behaviour for misuse, not Jolt's asserts.

## Options

- **Expose Jolt's API.** Fastest, but it ties every game system to Jolt's types, reference counting, layer interfaces and process-wide setup. ADR 0003 rules it out.
- **A large engine-agnostic physics layer now:** joints, characters, queries of every kind, events. Speculative before callers exist.
- **A small owned API covering what the next callers need,** with no Jolt types, and extended as callers appear.

## Decision

Proposed, not accepted by the owner:

- **`lykoware::physics::World`** (`engine/include/lykoware/physics.hpp`) owns one Jolt physics system and all its bodies. Jolt is linked privately into the engine, and only `engine/src/physics.cpp` includes it.
- **Stable handles.** `BodyId` is `{index, generation}` into a Lykoware slot table.
  - Freed slots are reused last-in first-out, so the order is deterministic, with a new generation. A destroyed body's handle stays invalid for the world's whole life and can never reach a later body.
  - We do not rely on Jolt's own `BodyID`, whose 8-bit sequence number repeats after 256 reuses of a slot.
  - The default handle is never valid.
- **Bodies.**
  - Box, sphere and capsule shapes;
  - static, dynamic or kinematic motion;
  - position, rotation (normalised; a zero quaternion is refused), initial velocities, friction and restitution.
  
  More shapes (meshes, height fields, compounds) arrive with their callers: height fields with the character in spike PR 3.
- **Misuse is defined, never a crash.**
  - Every call with an invalid, destroyed or foreign handle returns false or nullopt.
  - Bad shapes and limits are refused with a message: non-positive sizes, non-finite values, too many bodies, too little step memory.
  - Giving a static body a velocity is refused.
- **Stepping.** `step(seconds)` advances one fixed step. Callers step once per ADR 0017 tick with `FixedStep::tick_seconds()`. A failed step (out of step memory or contact space) returns false and leaves the world usable.
- **Queries.** `cast_ray(origin, direction)` returns the closest hit as a body handle, fraction and point. More queries arrive with callers.
- **Collision filtering.** Static bodies do not collide with each other; everything else collides. Game-defined layers wait for a caller.
- **Determinism.**
  - `state_hash()` hashes every live body's exact position, rotation and velocities in handle order.
  - Results do not depend on the worker-thread count (Jolt's guarantee; the test runs 1 and 4 threads).
  - The recorded hash must match in every CI job, as in ADR 0018.
- **Process-wide state.** Jolt's allocator, factory and type registration are set up by the first `World` and torn down by the last, under a lock. Several worlds may be alive at once. Jolt's trace messages go to `lykoware::log` as warnings, and its asserts (Debug builds) to the log as errors, without breaking into a debugger.
- **Units and axes:** metres, kilograms, seconds and radians; +Y up; right-handed; gravity a world setting (default 9.81 m/s² down).
- **Minimal value types.** `Vec3` and `Quat` live in the physics namespace until the engine has a maths module, which will replace them.

## Contracts and compatibility

Ownership, threading and failure are documented on the declarations:
- one thread at a time per world;
- worlds created and destroyed on one thread;
- nothing throws for misuse; `std::bad_alloc` can propagate.

`WorldConfig` defaults are 4096 bodies, 8192 body pairs and 8192 contact constraints, 16 MiB of step memory, and worker threads chosen from the CPU (1–8). Results do not depend on the worker count.

## Consequences

- Game systems depend only on Lykoware types. Replacing or upgrading Jolt touches one source file and its tests.
- Handle checks cost a bounds check and a compare per call.
- Features beyond this set (joints, characters, contact events, more queries, layers) need API additions, each with tests, when a caller needs them.

## Verification

- `physics.world` (`tests/physics_world.cpp`) links only `lykoware::engine`. It checks:
  - **Configuration:** invalid limits are refused.
  - **Resting contact:** a sphere comes to rest on the ground at the right height and goes still.
  - **Ray casts:** a ray hits the sphere's top at the expected fraction, another hits the ground, and a ray pointing away or with zero length hits nothing.
  - **Kinematic motion:** a kinematic body moves exactly one metre in one second at 1 m/s, ignoring gravity.
  - **Invalid handles:** the default handle, out-of-range indices, wrong generations, destroyed handles and double destroys all fail safely. A reused slot gets a new generation, and ray hits report the new handle.
  - **Bad bodies:** negative radius, flat box, capsule without a cylinder and zero rotation are refused, and nothing is created.
  - **Limits:** the body limit is enforced, and a place is freed by destroying a body.
  - **Cleanup:** five rounds of creating, stepping and destroying 200 bodies leave only the ground. Worlds are created and destroyed repeatedly.
  - **Determinism:** a 27-body scene of boxes, spheres and capsules plus ground, run for 240 ticks with 1 and with 4 worker threads, gives identical states, and the hash must equal `0x84f90a3cbf37a28b`.
- **Local:**
  - GCC 13 Debug and Release and Clang 18 Debug and Release all produce `0x84f90a3cbf37a28b`.
  - 27/27 tests pass in the Debug, ASan/UBSan and clang-tidy presets.
  - clang-tidy's padding check first flagged 75 bytes of padding in the world's private state. Jolt's process-wide setup is now a base class, which is guaranteed to be constructed first and destroyed last whatever the field order, and the stateless layer filters are shared constants. The fields could then be reordered.
- **Deliberate breaks, each caught:**
  - ignoring the generation in handle lookup failed "a wrong generation is invalid" and "the old handle never reaches the new body";
  - ray hits that drop the generation failed three ray checks.
- **CI:** pending.
