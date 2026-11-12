# Architecture baseline

Implementation and verification are tracked by commit/branch in [STATUS](STATUS.md); design decisions in [ADRs](adr/README.md). A merged implementation does not automatically accept a Proposed ADR.

## Dependency direction

Game applications, CLI tools and future Howler editor -> Lykoware runtime -> wrapped low-level vendored libraries. The runtime never imports game/editor code. The C++20 static target `lykoware::engine` exposes owned headers under `engine/include/lykoware`; vendor types stay private. CMake links native applications separately.

## Runtime boundaries

| Module | Responsibility and contract |
| --- | --- |
| Window/input | SDL 3.4.18 behind owned APIs; keyboard/mouse/gamepad, dummy/offscreen test modes; ADR 0008 |
| Clocks | Monotonic FrameClock and exact fixed-tick accumulator; 60 Hz default is proposed, alpha is presentation-only; ADR 0017 |
| ECS | Sparse-set packed component arrays, generation/owner handles, deterministic local iteration, deferred changes and removal hooks; ADRs 0023/0030 |
| Physics | Jolt 5.6.0 behind `physics::World`; double world positions, float velocities/rotations; state hashes, bodies, joints, height fields, walking characters and queries; ADRs 0018/0020/0022/0073 |
| Physics/ECS link | Exclusive cleanup-hook claim; physics world outlives linked ECS world; transactional attachment; cannot rebind or move linked worlds; ADR 0030 |
| Math/transform | Owned RH +Y-up math, double transform positions, quaternion rotation; ADRs 0024/0026 |
| RHI/render | Vulkan 1.3 loaded at runtime through volk; VMA allocations, owned buffers/pipelines/targets/swapchain, Slang ahead-of-time shaders; first cube renderer uses reversed depth and camera-relative floats; ADRs 0009–0016/0025/0026 |
| Scenes/commands | Versioned box-scene schema, explicit authored IDs and fixed-tick spawn/destroy/velocity handlers; nlohmann/json private; ADR 0031 |
| Presentation | Interpolated snapshot of previous/current authoritative transforms; double blend and camera-relative subtraction precede float GPU values; const ECS rendering |
| Skeletal presentation | Owned clip sampling, skinning, IK, interruptible fades and bounded authored graphs; events/sockets and strict-subset retargeting (#71); ADRs0049/0061–0065, see [animation runtime](ANIMATION_RUNTIME.md) |
| Diagnostics | CPU trace JSON, PPM readback/diff harness, scene capture and offscreen measurement reports; no claim of calibrated GPU/frame-pacing gates |

The scene owns physics first and ECS second so ECS cleanup runs before physics destruction. Runtime owner tokens are process-local capabilities, excluded from files/hashes; authored IDs are explicit strings independent of handles and process-local ECS type storage IDs. Destroyed authored IDs cannot be reused in the same scene lifetime. Versioned component names are explicit schema constants, never inferred from C++ type names or registration order.

A command stream sorts by (tick, sequence). Commands dispatch before the fixed tick's physics update. Rendering reads a const snapshot; interpolation/cameras cannot feed back into physics. The same public handler serves headless CLI and future script/editor actions. Deferred ECS closures remain internal runtime machinery, not a persisted command format. Future async inference/jobs must return bounded proposals through validated authoritative commands.

## Applications and verification

`lykoware_sandbox` retains foundation/CPU-trace and windowed Vulkan paths; the latter shows lit falling crates. `lyko` performs build/test/process routing and headless scene replay/capture/measurement. Build/install deploy validated SPIR-V beside each executable. Missing GPU hardware is an explicit skipped test, never a graphics pass. Linux CI uses Mesa lavapipe for actual render checks; Windows hosted GPU absence remains separate from native Windows compilation and simulation tests.

Tests include malformed-input/round-trip fixtures, independent-world and allocation-failure checks, deterministic physics hashes, per-tick replay equality under 30/60/144 Hz and irregular frames, read-only presentation/GPU rendering, far-origin pixel equality and CLI capture repeatability. Actual target-PC performance requires local evidence. See [PERFORMANCE](PERFORMANCE.md).

## Future boundaries

Generic assets/import, renderer scale/performance, jobs, streaming, save migrations, networking and game systems remain planned; current skeletal runtime capabilities are described above. The first scene schema is a box/cube vertical slice, not a general serialization framework. Howler and the content suite retain separate roadmap gates after the engine core. No full editor is introduced by the command path.

Before extending a shared API, record ownership/lifetime, failure and rollback, threading/update order, units/coordinates, persistent compatibility, standalone/editor callers and a representative verification scenario in an ADR. Keep interfaces tied to actual callers.
