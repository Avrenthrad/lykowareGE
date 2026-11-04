# Architecture baseline

Status: current foundation plus intended boundaries. Planned modules below do not imply implemented APIs.

## What exists today

| Target | Contents |
| --- | --- |
| lykoware_engine (alias lykoware::engine) | Static library: logging, CPU timer/trace, GPU-free image harness (not a renderer), window and input, RHI instance (`lykoware::rhi`, Vulkan loaded at run time), and generated version header |
| lykoware_sandbox | Console sample linked against the engine. No arguments runs the foundation check. `--frames N --trace FILE` runs N CPU-loop iterations and writes a Chrome trace. `--window` pumps a window until close. `--window --dummy --frames N` does that without a display |
| lyko | Native CLI. `build`/`test` call CMake/CTest presets. `run` launches lykoware_sandbox. `trace --frames N` records that sandbox CPU loop. `screenshot` and `run --scene` return an unsupported-renderer error. One dispatcher in tools/cli; not a command bus |
| foundation.smoke | Sandbox executable startup/CLI checks |
| cli.integration | Real CMake/CTest runs plus process/argument/failure integration checks |

The root CMake project builds these targets and stages the sample executable. SDL 3.4.18 (window and input) and Vulkan-Headers plus volk (RHI) are the vendored runtime sources; see [third_party/README.md](../third_party/README.md). Vulkan is loaded at run time, never linked. Python 3.10+ is a test-only dependency; clang-tidy 18 is an optional analysis tool.

## Intended dependency direction

Game applications -> Lykoware runtime
Howler editor/play mode -> Lykoware runtime
Content build/import tools -> shared asset definitions and processing libraries as needed

The runtime must not require Howler to be installed or link editor UI code. Engine code must not reference the sample game. Avoid circular dependencies.

## Planned responsibility boundaries

| Area | Engine/runtime owns | Howler/game owns |
| --- | --- | --- |
| Platform | Window, input events, clocks, files | Input bindings and application workflow |
| Rendering | Drawing, materials, cameras, GPU resources | Art assets, scene composition, authoring UI |
| World | Object identity, transforms, loading, lifecycle | Authored scenes and game-specific components |
| Simulation | Physics integration and reusable scheduling | Combat, crafting, progression, settlement rules |
| Assets | Loading and resource lifetime | Source content and authoring/import workflow |
| Persistence | Serialization facilities and format handling | Which game state persists and its meaning |
| Tooling | Inspection hooks and reusable runtime APIs | Panels, selection gizmos, undo/redo, project workspace |

Create modules incrementally. The brief requires a data-driven ECS; its design/library, scripting language, threading model, and plugin ABI remain open. The graphics API is Vulkan 1.3 behind an owned RHI ([ADR 0002](adr/0002-graphics.md)); the renderer is not implemented. Authored encodings are accepted in [ADR 0004](adr/0004-data-formats.md); schemas are not.

## Shared contract checklist

Before another module relies on a new API, document:

- Ownership and object lifetime.
- Initialization/shutdown order and error reporting.
- Thread restrictions and update/event ordering.
- Units, coordinate conventions, and time representation where relevant.
- Data identifiers and persistence compatibility where relevant.
- How the same capability works in a standalone game and future editor play mode.
- A representative verification scenario.

Use an [ADR](adr/README.md) for consequential decisions. Avoid inventing shared interfaces that no implemented caller needs.

The build/test/run CLI is described in [ADR 0005](adr/0005-native-cli.md) and [CLI.md](CLI.md). Its dispatcher validates the small fixed command set. It does not implement the future engine mutation/undo command bus.

Physics lives in the engine library behind `lykoware::physics` ([ADR 0020](adr/0020-physics-api.md), [engine/include/lykoware/physics.hpp](../engine/include/lykoware/physics.hpp)). Jolt Physics (ADR 0003, ADR 0018) is linked privately and only `engine/src/physics.cpp` includes it; game and tool code see Lykoware handles and value types only.

The simulation clock lives in the engine library too: [ADR 0017](adr/0017-clocks-fixed-step.md) and [engine/include/lykoware/clock.hpp](../engine/include/lykoware/clock.hpp). `FixedStep` turns real frame times into numbered fixed-length ticks; simulation code advances only per tick and drawing blends the last two ticks by `alpha`. `FrameClock` measures real time between frames.

The CPU timer and trace live in the existing engine library, not a separate module. [ADR 0006](adr/0006-cpu-trace.md) and [engine/include/lykoware/trace.hpp](../engine/include/lykoware/trace.hpp) record ownership, lifetime, threading, and failure. Zones are single-threaded steady-clock measurements. The file is hand-written Chrome Trace Event JSON for diagnostics. It is not an authored project format. ADR 0004 accepts canonical JSON, TOML config, and glTF 2.0 meshes; this trace is not that format. There are no GPU events. Provisional CPU-only CI thresholds for the pinned hosted runners are in the root `CMakeLists.txt`; see [PERFORMANCE.md](PERFORMANCE.md).

The image harness lives in that same library. [ADR 0007](adr/0007-image-harness.md) and [engine/include/lykoware/image.hpp](../engine/include/lykoware/image.hpp) record ownership, lifetime, threading, and failure. It is a CPU-side RGB image, a capture interface, a hand-written PPM P6 reader and writer, and a per-pixel comparison with a tolerance and a diff image. The only capture is a deterministic software test pattern. CTest owns the golden files under `tests/golden/` and writes its outputs under the build directory. `lyko screenshot` is unchanged and still reports that no renderer exists. A later Vulkan offscreen capture can implement the same capture interface. [ADR 0002](adr/0002-graphics.md) accepts that RHI and records the plug-in; this harness does not implement it. Real screenshot tests are still waiting on a renderer. The PPM file is not an authored project format.

The window and input API lives in that same library. [ADR 0008](adr/0008-window-input.md) is proposed. [engine/include/lykoware/window.hpp](../engine/include/lykoware/window.hpp) and [engine/include/lykoware/input.hpp](../engine/include/lykoware/input.hpp) record ownership, lifetime, threading, and failure. SDL types stay in the engine sources. `VideoDriver::Dummy` is the headless path. The Vulkan surface hook takes an opaque instance pointer and does not link Vulkan. ADR 0002 is accepted; this wrapper does not implement the RHI.

The RHI starts in that same library under `lykoware::rhi`. [ADR 0009](adr/0009-vulkan-loading.md) is proposed. [engine/include/lykoware/rhi/instance.hpp](../engine/include/lykoware/rhi/instance.hpp) records ownership, lifetime, threading, and failure. volk opens the system Vulkan loader at run time; only `engine/src/rhi/` includes Vulkan headers. One instance per process. `rhi::Device` ([ADR 0010](adr/0010-rhi-device.md), proposed) selects a Vulkan 1.3 physical device with dynamic rendering and synchronization2 and creates one graphics-and-compute queue; one device per process. The device owns a Vulkan Memory Allocator instance and a blocking one-shot submit. `rhi::RenderTarget` ([ADR 0011](adr/0011-offscreen-render-target.md), proposed) is an RGBA8 offscreen target cleared through dynamic rendering and read back into the image harness via `RenderTargetCapture`. `rhi::Swapchain` ([ADR 0012](adr/0012-swapchain.md), proposed) presents the same clear pass to a window with two frames in flight; `VideoDriver::Offscreen` lets CI run it headless. Shaders are Slang, compiled ahead of time by host tools built from vendored source (`slangc -O0` → `spirv-opt` → `spirv-val`, [ADR 0014](adr/0014-shader-build.md), proposed); `rhi::Shader` and `rhi::GraphicsPipeline` ([ADR 0015](adr/0015-graphics-pipeline.md), proposed) load that SPIR-V and draw through the same dynamic-rendering pass as clears; `RenderTarget::draw` is the first user.

## Engine-first command boundary

Design a minimal serializable command dispatcher early. CLI and scripting use the same validated handlers; later editor actions extend those handlers. UI must not become a separate source of simulation truth. Async jobs and inference return bounded validated proposals; they do not directly mutate authoritative game state.

Low-level dependencies are vendored under third_party and hidden behind owned interfaces. Build only modules with actual callers.

## Next implementation boundary

Phase 0 continues the accepted C++20/CMake foundation with minimal native tooling and quality checks. Phase 1 then provides runtime lifecycle/window/input/ECS/rendering and minimum CLI/capture tooling. See [ROADMAP.md](ROADMAP.md) for sequencing. Track E begins only after Phase 6; Track C follows E2.
