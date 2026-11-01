# Lykoware (lykowareGE)

Lykoleon Studios' Windows-first, AI-native engine. Standard: **No limits. Just excellence.**
Build the flagship first: open-world RPG, first-/third-person action, structural sandbox construction, and RTS-scale battles.

The project's [master brief](docs/LYKOWARE_BRIEF.md) is the source of direction for the engine.

## Core rule: full ownership

Lykoleon builds and controls the original engine, custom editor, content tools, asset pipeline and agent integration. No dependency on Unity/Unreal/Godot/Blender or their editor plugins. Pragmatic low-level MIT/BSD/Apache/zlib libraries must be vendored as source forks under third_party, retain upstream terms, and sit behind replaceable Lykoware interfaces. No copyleft in shipped code or proprietary middleware lock-in.

Every tool/content operation must be reachable through CLI and scripting, with diffable authored data. Runtime **Phases 0–6 first**, full editor **Track E later**, built-in content suite **Track C after E2**. Early tools are CLI, debug overlay, profiler and capture only.

## Start here

- [Status: implemented, current, next, known issues](docs/STATUS.md)
- [Owner vision and remaining questions](docs/ENGINE_VISION.md)
- [Roadmap and exit criteria](docs/ROADMAP.md)
- [Architecture](docs/ARCHITECTURE.md) and [data ownership](docs/DATA_FORMATS.md)
- [ADRs 0000–0004](docs/adr/README.md)
- [Performance targets and evidence requirements](docs/PERFORMANCE.md)
- [Shared agent instructions](AGENTS.md), [contributing](CONTRIBUTING.md), [handoffs](docs/WORK_BOARD.md)
- [Build instructions](docs/BUILDING.md) and [checklist](CHECKLIST.md)

## What actually runs

A C++20/CMake engine library and console sample with logging and smoke tests. Windows Debug/Release and Linux checks passed in [baseline CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
There is no window, renderer, physics, CLI scene/capture command or editor yet.

Joshua requested **C++20 versus Rust comparison before choosing**. Preserve the working baseline; Vulkan, wgpu/ash, Jolt and RON/TOML/JSON are proposals under ADR evaluation.

## Product boundaries and release policy

Lykoleon Studios is the studio; Lykoware is the runtime; Howler: Initiation is the future custom development application; Pax is future anti-cheat and the required public-release review gate.

Private development; GitHub-only repositories. Nothing public without Pax review and owner authorization. No personal data to outside AI services. Any spend needs prior approval. See [ownership policy](docs/adr/0000-ownership-and-licensing.md) and [proprietary notice](LICENSE).

Keep the current repository name until Joshua confirms a rename.
