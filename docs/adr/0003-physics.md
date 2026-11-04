# ADR 0003 — Physics integration

Status: **Accepted by owner 2026-11-04.**
- Jolt Physics v5.6.0, used natively from C++ behind a Lykoware physics API.
- The two *Numerical Recipes*-derived functions are replaced by our own implementations, as a recorded local patch.
- Cross-platform determinism is on from the start.
- Float precision for now, revisited with the world-scope decision.

The build spike follows. The licence and fit review that led to this is below.

Related task/PR: physics decision. Review in [#20](https://github.com/Avrenthrad/lykowareGE/pull/20); integration in the spike PRs that follow.

## Options and boundary

Evaluate Jolt under its exact permissive source licence. C++20 is accepted in ADR 0001, so evaluate a wrapped native API without Rust bindings. Do not expose third-party physics types through game/tool contracts.

Lykoware owns stable IDs, lifecycle, collision filtering, queries, world stepping and event translation. Define safe destruction, callbacks, thread ownership and unit conventions. Deterministic tests have a declared platform/toolchain scope; do not assume cross-platform bit-identical physics.

## Evidence and exit

Vendor a pinned source revision and notices. Verify body create/destroy, fixed-step simulation, queries and character/terrain contact, repeated cleanup, and invalid-handle behavior. Benchmark representative active bodies and structural destruction before promising full-scale construction simulation. Document how active ragdoll/IK will integrate; a rigid-body integration alone is not Euphoria-like animation.

## Review, 2026-11-04

Read from upstream sources on 2026-11-04. This is not legal advice. Anything outside the ADR 0000 list (MIT, BSD, Apache, zlib) is the owner's call, and, if wanted, a lawyer's.

### Candidates

| Candidate | Licence | State | Fit |
| --- | --- | --- | --- |
| **Jolt Physics v5.6.0** (commit `e77f175`, 2026-07-11) | MIT | Active; one main author; used by *Horizon Forbidden West* and *Death Stranding 2* (per its README) | Native C++17, small (5.7 MB of source), deterministic options, characters, ragdolls with motors. Two files need a decision (below). |
| PhysX (NVIDIA, repository updated 2026-10-15) | BSD-3-Clause | Active | Large repository (PhysX, Blast and Flow). Its GPU features need CUDA, which only NVIDIA GPUs have; the MSI Claw's Intel Arc GPU would run CPU-only. |
| Bullet 3.25 | zlib (except `Extras/` and third-party examples) | No release since 3.25; maintenance commits only | Older design; less suited to new work. |
| Box2D, Rapier | MIT / Apache-2.0 | — | Box2D is 2D only. Rapier is Rust (ADR 0001 chose C++). |
| Write our own | — | — | Years of work before it matched any of the above. |

### Jolt licence inventory

- `Jolt/` (the library) has 559 files.
  - 556 carry `SPDX-License-Identifier: MIT` and are copyright Jorrit Rouwe.
  - The other 3 are a CMake file, a debugger-display file and a diagram, covered by the repository's MIT `LICENSE`.
- The only non-MIT files in the repository are Gradle's Android build wrappers (`Build/Android/gradlew*`, Apache-2.0). We would not vendor them.
- The build downloads nothing.
  - Its optional GPU "compute" backends (DirectX 12, Vulkan, Metal) need Microsoft's DXC shader compiler (NCSA licence), and only Jolt's hair simulation uses them.
  - We would switch them off, as we did with Slang's optional parts (ADR 0013).
- **Owner decision needed: code adapted from *Numerical Recipes*.** Two MIT-tagged files say they were "Taken and adapted from Numerical Recipes":
  - `Jolt/Math/EigenValueSymmetric.h` (§11.1, Jacobi eigenvalues). Used by `MassProperties` for every dynamic body's inertia, so it cannot be switched off.
  - `Jolt/Math/GaussianElimination.h` (§2.1, Gauss–Jordan). Used by `Matrix::SetInversed` and the wheeled-vehicle controller.
  
  The eigenvalue routine follows the book's `jacobi` closely: the same structure, variable names, threshold rule and rotation macro, with "Original code:" comments. The book's code is copyrighted and not under a permissive licence; the algorithms themselves are standard textbook mathematics. No Jolt issue or pull request mentioning Numerical Recipes was found.
- **Lower risk, recorded for completeness:**
  - four vectorised maths functions (`Vec4::SinCos`, `Tan`, `ASin`, `ATan`) are "based on" the Cephes library, which its author distributes freely and which permissive projects such as SciPy bundle;
  - several geometry routines cite Ericson's *Real-Time Collision Detection* and published papers (for example van den Bergen's GJK). These are algorithm references.

### Fit to Lykoware's requirements

- **Language and build:** C++17 in our C++20 build; its own CMake options; RTTI and exceptions off by default; warnings as errors upstream.
- **Fixed step:** `PhysicsSystem::Update(delta, collision_steps)` takes a fixed step, so it runs once per ADR 0017 tick.
- **Determinism:**
  - Jolt repeats results exactly when the same calls are made in the same order with the same binary.
  - Its `CROSS_PLATFORM_DETERMINISTIC` option, about 8% slower, makes results match across MSVC, Clang and GCC, Windows and Linux, and x86 and ARM. Upstream verifies this on every build.
  - Caveats: broad-phase query order, contact and activation callbacks (called from worker threads) and active-body lists are not ordered. Lykoware's wrapper must sort them before game code sees them.
- **Replay and rollback:** `PhysicsSystem::SaveState` and `RestoreState` record and restore the whole simulation. That serves replay fixtures and co-op/MMO rollback.
- **Big worlds:**
  - Floats are accurate within about 5 km of the origin.
  - `JPH_DOUBLE_PRECISION` (5–10% slower) keeps accuracy at thousands of kilometres.
  - Which one we need depends on the world-scope decision still open.
- **Characters:**
  - `CharacterVirtual` is a kinematic capsule for player control (steps, slopes, standing on moving bodies);
  - `Character` is a rigid-body capsule;
  - height-field terrain and triangle meshes are supported.
- **Active ragdoll (Phase 2):**
  - ragdolls can be driven to an animated pose by constraint motors or kinematically;
  - a skeleton mapper maps between animation and ragdoll skeletons;
  - swing-twist and six-degree-of-freedom constraints have motors.
  
  These are building blocks. Stumble and recovery, balance and blending remain Lykoware work, as this ADR already says.
- **Building and destruction (Phase 4):**
  - rigid bodies, compounds and breakable constraints are available;
  - there is no structural load or support solver, so Lykoware builds that model itself;
  - the active-body benchmark in "Evidence and exit" still applies.
- **Threads:** Jolt runs on a job-system interface. Its own thread pool serves until Lykoware has a job system.
- **Not needed now:** vehicles, soft bodies and GPU hair. They stay compiled out or unused.

### Recommendation

Adopt Jolt Physics v5.6.0, vendored unmodified (except as decided below) with provenance, and wrapped behind `lykoware::physics` with no Jolt types in public headers.

### Owner decisions (answered 2026-11-04: yes to each recommendation)

1. **Jolt** (recommended) or another candidate?
2. **The two Numerical Recipes-derived files.** Options:
   - accept them as they are;
   - replace both functions in our vendored copy with our own implementations, written from the textbook algorithms and recorded as a local patch with tests that compare results;
   - in either case, ask upstream about them.
3. **Cross-platform determinism** from the start (about 8% slower)? Recommended, given the co-op/MMO leaning and that CI already runs on Windows and Linux.
4. **Precision:** floats now, or double precision? Recommended: floats now, revisited with the world-scope decision. The switch is a build option, but every platform must use the same setting.

### Spike once decided

- Vendor `Jolt/` with provenance, built in all five CI jobs with its compute backends off.
- Wrap it behind `lykoware::physics`.
- Tests:
  - body create and destroy;
  - stepping once per `FixedStep` tick;
  - a ray-cast query;
  - a character capsule on a height field;
  - repeated cleanup;
  - invalid handles;
  - determinism: the same scene run twice gives an identical state hash, and Windows and Linux hashes are compared when cross-platform determinism is on;
  - a first active-body benchmark on the hosted runners, labelled as not a hardware profile.
