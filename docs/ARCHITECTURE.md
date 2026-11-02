# Architecture baseline

Status: current foundation plus intended boundaries. Planned modules below do not imply implemented APIs.

## What exists today

| Target | Contents |
| --- | --- |
| lykoware_engine (alias lykoware::engine) | Static library: logging and generated version header |
| lykoware_sandbox | Console sample linked against the engine |
| lyko | Native build/test CLI; subprocess runner is private to tools/cli |
| foundation.smoke | Sandbox executable startup/CLI checks |
| cli.integration | Real CMake/CTest runs plus process/argument/failure integration checks |

The root CMake project builds these targets and stages the sample executable. There are no third-party runtime dependencies. Python 3.10+ is a test-only dependency; clang-tidy 18 is an optional analysis tool.

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

The build/test CLI is described in [ADR 0005](adr/0005-native-cli.md). It does not implement the future engine mutation/undo command bus.

## Engine-first command boundary

Design a minimal serializable command dispatcher early. CLI and scripting use the same validated handlers; later editor actions extend those handlers. UI must not become a separate source of simulation truth. Async jobs and inference return bounded validated proposals; they do not directly mutate authoritative game state.

Low-level dependencies are vendored under third_party and hidden behind owned interfaces. Build only modules with actual callers.

## Next implementation boundary

Phase 0 continues the accepted C++20/CMake foundation with minimal native tooling and quality checks. Phase 1 then provides runtime lifecycle/window/input/ECS/rendering and minimum CLI/capture tooling. See [ROADMAP.md](ROADMAP.md) for sequencing. Track E begins only after Phase 6; Track C follows E2.
