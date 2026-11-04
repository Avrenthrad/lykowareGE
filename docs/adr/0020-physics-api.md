# ADR 0020 — The Lykoware physics API

Status: **Accepted by owner 2026-11-04**, as merged in #23–#25. Amended by [ADR 0022](0022-double-precision-positions.md): positions are double precision (as [ADR 0021](0021-product-targets.md) decides), kinematic bodies never sleep, and bodies with non-finite positions or velocities are refused. `Vec3`, `DVec3` and `Quat` are now aliases of the maths module's types ([ADR 0024](0024-math.md)).
Related task/PR: physics spike, PR 2 (merged as [#23](https://github.com/Avrenthrad/lykowareGE/pull/23)); extended by spike PR 3 (branch `claude/physics-character`) with terrain and characters, and by spike PR 4 (branch `claude/physics-benchmark`) with step memory that overflows to the heap and a body-count benchmark.
Numbered 0020 because a concurrent documentation proposal ([#22](https://github.com/Avrenthrad/lykowareGE/pull/22)) claimed 0019 first.
Supersedes / superseded by: none. Builds on [ADR 0003](0003-physics.md), [ADR 0017](0017-clocks-fixed-step.md) and [ADR 0018](0018-jolt-integration.md).

## Context

ADR 0003 requires that no third-party physics type crosses into game or tool code. It also says Lykoware owns stable IDs, lifecycle, collision filtering, queries, world stepping, units and failure behaviour. Spike PR 1 proved that Jolt builds and is deterministic, but its tests call Jolt directly. Game code needs a small, owned API with defined behaviour for misuse, not Jolt's asserts.

## Options

- **Expose Jolt's API.** Fastest, but it ties every game system to Jolt's types, reference counting, layer interfaces and process-wide setup. ADR 0003 rules it out.
- **A large engine-agnostic physics layer now:** joints, characters, queries of every kind, events. Speculative before callers exist.
- **A small owned API covering what the next callers need,** with no Jolt types, and extended as callers appear.

## Decision

Accepted by the owner on 2026-11-04:

- **`lykoware::physics::World`** (`engine/include/lykoware/physics.hpp`) owns one Jolt physics system and all its bodies. Jolt is linked privately into the engine, and only `engine/src/physics.cpp` includes it.
- **Stable handles.** `BodyId` is `{index, generation}` into a Lykoware slot table.
  - Freed slots are reused last-in first-out, so the order is deterministic, with a new generation. A destroyed body's handle stays invalid for the world's whole life and can never reach a later body.
  - A slot whose generation has reached 2^32 - 1 is retired when freed, not reused, as in the ECS ([ADR 0023](0023-ecs.md)). The code merged in [#23](https://github.com/Avrenthrad/lykowareGE/pull/23) wrapped back to generation 1 instead, which broke the promise above after 2^32 - 1 reuses of one slot. Fixed after acceptance, with no change below that count.
  - We do not rely on Jolt's own `BodyID`, whose 8-bit sequence number repeats after 256 reuses of a slot.
  - The default handle is never valid.
- **Bodies.**
  - Box, sphere and capsule shapes;
  - static, dynamic or kinematic motion;
  - position, rotation (normalised; a zero quaternion is refused), initial velocities, friction and restitution.
  
  More shapes (meshes, height fields, compounds) arrive with their callers: height fields with the character in spike PR 3.
- **Misuse is defined, never a crash.**
  - Every call with an invalid, destroyed or foreign handle returns false or nullopt.
  - Bad shapes and limits are refused with a message: non-positive sizes, non-finite values, too many bodies, step memory outside 64 KiB to 1 GiB.
  - Giving a static body a velocity is refused.
- **Stepping.** `step(seconds)` advances one fixed step. Callers step once per ADR 0017 tick with `FixedStep::tick_seconds()`. A failed step (out of body-pair or contact space) returns false and leaves the world usable. A step that needs more scratch memory than `step_memory_bytes` takes the rest from the heap (spike PR 4).
- **Queries.** `cast_ray(origin, direction)` returns the closest hit as a body handle, fraction and point. More queries arrive with callers.
- **Collision filtering.** Static bodies do not collide with each other; everything else collides. Game-defined layers wait for a caller.
- **Determinism.**
  - `state_hash()` hashes every live body's exact position, rotation and velocities in handle order.
  - Results do not depend on the worker-thread count (Jolt's guarantee; the test runs 1 and 4 threads).
  - The recorded hash must match in every CI job, as in ADR 0018.
- **Process-wide state.** Jolt's allocator, factory and type registration are set up by the first `World` and torn down by the last, under a lock. Several worlds may be alive at once. Jolt's trace messages go to `lykoware::log` as warnings, and its asserts (Debug builds) to the log as errors, without breaking into a debugger.
- **Units and axes:** metres, kilograms, seconds and radians; +Y up; right-handed; gravity a world setting (default 9.81 m/s² down).
- **Minimal value types.** `Vec3` and `Quat` live in the physics namespace until the engine has a maths module, which will replace them.

### Terrain and characters (spike PR 3)

- **Height fields.**
  - `HeightFieldShape` is a square grid of heights (row-major), `spacing` metres apart along +X and +Z from the body's position.
  - Static bodies only. `samples_per_side` must be even, from 4 to 4096. Wrong sample counts, non-finite heights and non-positive spacing are refused.
  - Jolt stores heights compressed, so we ask for at most 1 cm of error.
- **Characters.** `World::create_character` makes an upright capsule (Jolt's `CharacterVirtual`) with its own generation-checked `CharacterId`.
  - **Position:** its position is its feet.
  - **Ground state:** `character_state` reports `OnGround`, `OnSteepGround`, `NotSupported` or `InAir`, plus the ground body's handle and the ground normal.
  - **Limits:** `max_slope` (default 50°) and `step_height` (default 0.4 m) set what it can walk up.
  - **Input:** `set_character_input(id, horizontal_velocity, jump_speed)` sets the speed it tries to move at from now on. A jump is used once, on the next step where it is on walkable ground.
  - **Movement:** each step the world adds gravity and the ground's own motion, then moves characters in handle order before stepping bodies.
  - **Not yet:**
    - dynamic bodies do not push characters (no inner body);
    - characters do not collide with each other;
    - nothing tests characters pushing bodies.
    
    These come with a caller.
- Shapes are now built through Jolt's shape settings, which also cover height fields. `physics.world`'s hash did not change, so the switch changed no behaviour.
- Jolt numbers characters from a process-wide counter. The determinism test creates its scene twice in one process (so with different character numbers) and requires identical states.

### Step memory and body count (spike PR 4)

- **The problem.** Jolt's plain `TempAllocatorImpl` calls `std::abort()` when a step needs more scratch memory than was reserved. That ends the process even in Release, so the old "returns false" promise did not hold. The body-count benchmark found it: 4096 colliding boxes need more than the default 16 MiB.
- **The fix.** The world uses `TempAllocatorImplWithMallocFallback`. The reserved block is used first and anything beyond it comes from the heap. A big scene is slower, not fatal.
- **Limits.** `step_memory_bytes` must be from 64 KiB to 1 GiB. Jolt takes the size as a 32-bit number, so the upper limit also stops a large value from wrapping round to a tiny one.
- **Benchmark.** `lykoware_physics_benchmark BODIES TICKS LIMIT_MS` drops layers of 16 x 16 small boxes, each turned a little, onto the ground and times every step. It prints the mean, 95th-percentile and worst step times and how many boxes still move at the end. It fails if a step fails, a box falls through the ground, or the mean step time is over the limit. CTest runs it as `perf.physics_bodies_256` and `perf.physics_bodies_1024`, provisional and CPU-only like the other `perf.*` checks ([PERFORMANCE.md](../PERFORMANCE.md)).

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
- **Terrain and characters** (`physics.character`, spike PR 3, links only the engine):
  - **Landing and walking:** a character dropped onto flat terrain lands with its feet at height 0 and reports the terrain as its ground. It walks 2 m in 1 s at 2 m/s.
  - **Jumping:** a 5 m/s jump leaves the ground, lands again, and is used only once.
  - **Slopes:** it walks up a 15° slope but is held at the foot of a 63° one.
  - **Ledges:** it walks up a 0.3 m ledge but a 0.7 m ledge blocks it.
  - **Rays:** rays hit the height field.
  - **Misuse:** every invalid character handle and bad character or height-field description is refused.
  - **Determinism:** a rolling-terrain scene with three characters (one jumping) and six crates gives the same state with 1 and 4 worker threads, and its hash must be `0xbe1264481a8a8720`. The terrain is a triangle wave built only from operations that round identically everywhere, because `std::sin` differs between platforms' maths libraries.
  - **Local results:** GCC 13 and Clang 18 Debug and Release give that hash; 28/28 tests pass in the Debug, ASan/UBSan and clang-tidy presets.
  - **Deliberate breaks, each caught:** ignoring `step_height` failed the 0.3 m ledge; ignoring `max_slope` failed the 63° slope check and the 0.7 m ledge; a jump that is never used up failed "lands again".
  - **A gap found by a break:** my first steep-slope check ran out of time before the character reached the steep part. With the 85° break it had only just started climbing, so the break was caught only by the ledge check. The check now pushes for six seconds and requires the character to stay at the foot.
- **Step memory and body count** (spike PR 4):
  - **Regression test** (in `physics.world`): a world with only 64 KiB of reserved step memory steps 300 boxes for 60 ticks. With Jolt's plain allocator this aborted with "TempAllocator: Out of memory trying to allocate 131072 bytes" (exit 134); now it passes. A request for 2 GiB is refused.
  - **Benchmark, local** (4 hardware threads, GCC 13): see the table in [PERFORMANCE.md](../PERFORMANCE.md). In Release, 1024 colliding boxes take 0.72 ms per step on average and 4.1 ms at worst; 4096 take 9.9 ms on average.
  - **Hashes unchanged:** `physics.world` and `physics.character` still give `0x84f90a3cbf37a28b` and `0xbe1264481a8a8720`, so the allocator change did not change results.
- **CI (spike PR 4):** [run 41771495786](https://github.com/Avrenthrad/lykowareGE/actions/runs/41771495786) on d58e187 passed all five jobs, with both physics hashes unchanged. The 1024-box benchmark took 0.27 s for 300 steps on Windows Release and 12.4 s under ASan/UBSan, far inside the 250 ms-per-step limit.
- **CI (spike PR 3):** [run 41770820818](https://github.com/Avrenthrad/lykowareGE/actions/runs/41770820818) on 3d40d91 passed all five jobs. `physics.character` reproduced `0xbe1264481a8a8720` on Windows MSVC Debug and Release and on Linux GCC Debug, ASan/UBSan and the clang-tidy build.
- **CI (spike PR 2):** [run 41765080887](https://github.com/Avrenthrad/lykowareGE/actions/runs/41765080887) on 9204b58 passed all five jobs. `physics.world` reproduced `0x84f90a3cbf37a28b` on Windows MSVC Debug and Release and on Linux GCC Debug, ASan/UBSan and the clang-tidy build.
