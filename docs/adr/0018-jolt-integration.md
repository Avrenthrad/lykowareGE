# ADR 0018 — Building and linking Jolt Physics

Status: **Accepted by owner 2026-11-04**, including the AVX2 CPU requirement.
Related task/PR: physics spike, PR 1 (branch `claude/physics-spike`), after [ADR 0003](0003-physics.md) was accepted on 2026-11-04.
Supersedes / superseded by: superseded in part by [ADR 0022](0022-double-precision-positions.md), which turns on `DOUBLE_PRECISION` (owner decision in [ADR 0021](0021-product-targets.md)). Implements the build side of ADR 0003.

## Context

The owner accepted Jolt Physics v5.6.0 with cross-platform determinism, float precision and local replacements for the two *Numerical Recipes*-derived functions (ADR 0003). Jolt has to build inside the engine's CMake on all five CI jobs (MSVC, GCC, sanitizers, clang-tidy) without changing how the engine's own code is compiled, without downloads, and with exactly the same options everywhere. Cross-platform determinism holds only if every platform compiles the same source with the same defines.

## Options

- **Write our own CMake for Jolt's sources.** Full control, but we would have to track Jolt's source list and per-compiler flags on every update.
- **Use upstream's `Build/CMakeLists.txt` as a subdirectory, with options forced.** This is the integration Jolt documents. Upstream maintains the source list and the per-compiler determinism flags.
- **Prebuilt binaries.** Rejected: ADR 0000 requires vendored source.

## Decision

Accepted by the owner on 2026-11-04:

- **Vendoring.**
  - Jolt v5.6.0's `Jolt/`, `Build/CMakeLists.txt`, `Build/README.md`, `LICENSE` and `README.md` are vendored under `third_party/jolt`, with provenance and hashes.
  - The two *Numerical Recipes*-derived headers are replaced by Lykoware implementations: cyclic Jacobi eigenvalues, and Gauss–Jordan with row pivoting.
- **Build.** `cmake/VendorJolt.cmake` adds `third_party/jolt/Build` as an `EXCLUDE_FROM_ALL SYSTEM` subdirectory. Jolt builds only when something links it, and its headers are system headers, so engine warnings and clang-tidy do not report inside them. Forced options:
  - **ADR 0003's decisions:**
    - `CROSS_PLATFORM_DETERMINISTIC=ON`: Jolt then compiles with precise floating point, no fused multiply-add and no FMA instructions;
    - `DOUBLE_PRECISION=OFF`.
  - **Consistency with the engine:**
    - a static library;
    - the engine's MSVC runtime (upstream defaults to the static runtime, which would not link);
    - no link-time optimisation;
    - RTTI on, so sanitizer builds can check calls into Jolt's polymorphic classes;
    - CMake's own per-configuration flags;
    - Jolt's warnings not promoted to errors (vendored code, as with SDL);
    - no install rules.
  - **Not used yet:** the debug renderer, the profiler, object streams, and GPU compute. Compute only serves hair simulation, and its backends need DXC, which is not vendored.
  - **Upstream defaults kept:**
    - C++ exceptions off inside Jolt;
    - Jolt's asserts on in Debug builds;
    - floating-point exception support;
    - 16-bit object layers.
- **Sanitizers.** In the sanitizer preset, Jolt itself is compiled with AddressSanitizer and UndefinedBehaviorSanitizer, as SDL is.
- **CPU requirement (owner attention).** Jolt's default instruction-set flags (AVX2, BMI, LZCNT, F16C, POPCNT) are kept, and Jolt applies them to every target that links it.
  - The engine therefore requires an x86-64 CPU with AVX2: Intel since 2013 (Haswell), AMD since Excavator and every Zen. The MSI Claw (Intel Core Ultra), a current RTX 4070 desktop CPU and the CI runners all qualify. An older CPU would stop with an illegal-instruction error.
  - Lowering the requirement (for example to SSE4.2) is a build option. It costs speed but not determinism: Jolt verifies identical results across instruction sets.
- **Determinism is a test, not an assumption.**
  - `physics.jolt_determinism` steps a fixed 64-body scene for 300 ticks at 60 Hz (ADR 0017) and requires one recorded state hash.
  - That hash must be the same in every CI job: MSVC and GCC, Debug and Release, sanitizers. It is the cross-platform check.
  - Re-baselining the hash is deliberate. A Jolt update or a change to these options records the new value and the reason in this ADR.
- **No public Jolt types.** Until the `lykoware::physics` wrapper lands (the next spike PR), only tests include Jolt.

## Contracts and compatibility

- Building any target that links Jolt adds Jolt's public defines (`JPH_CROSS_PLATFORM_DETERMINISTIC`, `JPH_USE_*`, `JPH_OBJECT_LAYER_BITS`, and others) and its instruction-set flags. Code that includes Jolt headers must be compiled with exactly those, which CMake does by linking the `Jolt` target.
- Jolt's process-wide setup (allocator, factory, type registration) is the caller's job. The wrapper will own it.

## Consequences

- Physics source is pinned and offline. About 5.7 MB of source is added, and a Jolt build takes about 20 seconds on 4 cores.
- The two replacement files must be carried across Jolt updates (see the provenance record).
- AVX2 becomes a stated hardware requirement, to be confirmed when hardware profiles are agreed.

## Verification

- **Local** (Linux, network blocked except for the initial clone):
  - **GCC 13 Debug and Release, and Clang 18 Debug and Release,** all produce state hash `0x17e8264fc6fa9cd7`, now the recorded value.
  - `physics.jolt_math` passes:
    - 3×3 and 4×4 solves, a permutation matrix that needs row swaps, a singular matrix refused, and 50 random matrices with reversed rows;
    - eigen decompositions of 200 random symmetric matrices, rebuilt and checked orthonormal;
    - a box's principal moments of inertia recovered after rotation.
  - `physics.jolt_determinism` passes: two runs match, save and restore at tick 150 reproduces the state, and no body ends below the ground.
  - **All 26 tests pass in the Debug, ASan/UBSan and clang-tidy presets.**
    - The sanitizer build instruments Jolt itself and reported nothing.
    - clang-tidy's static analyzer first reported a use-after-free inside Jolt's `Reference.h`. Its path assumed a shape's reference count fell to zero while the test still held a second reference: Jolt increments the count in compiled code the analyzer cannot see. That is a false positive (the instrumented run of the same code was clean). The test now hands each new shape straight to its body settings, so there is one owner, and the check stays on.
- **Deliberate breaks, each caught:**
  - turning cross-platform determinism off (GCC Release) changed the hash to `0x3808e44054d3a2ef`;
  - a sign error in the eigen rotation failed 200 checks, and Jolt's own inertia assert fired;
  - removing the row swap from Gauss–Jordan failed the permutation and reversed-row checks.
  
  An earlier version of the test used 2×2 matrices for the swap and singular cases. Jolt inverts 2×2 with a closed formula, so those cases never reached Gauss–Jordan; the third break exposed this, and the cases are now 3×3 and 4×4.
- **CI:** [run 41762141044](https://github.com/Avrenthrad/lykowareGE/actions/runs/41762141044) on a443f2a passed all five jobs.
  - `physics.jolt_determinism` reproduced `0x17e8264fc6fa9cd7` on **Windows MSVC Debug and Release**, and on Linux GCC Debug, ASan/UBSan and the clang-tidy build.
  - With the local GCC and Clang results, the scene is bit-identical across three compilers, two operating systems and Debug and Release builds.
