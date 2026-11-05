# Native lyko CLI

C++20 developer tool; CMake/CTest use child processes without shell interpretation. See [BUILDING](BUILDING.md), [ADR 0005](adr/0005-native-cli.md), [ADR 0031](adr/0031-scenes-and-command-replay.md), and [STATUS](STATUS.md) for branch/verification evidence.

```text
lyko build [--source DIR] [--configure-preset NAME] [--preset NAME] [--cmake EXECUTABLE]
lyko test [--source DIR] [--preset NAME] [--ctest EXECUTABLE]
lyko run
lyko run --scene FILE [--commands FILE] [--ticks N] [--capture FILE] [--report FILE]
lyko screenshot --scene FILE --output FILE [--commands FILE] [--ticks N] [--report FILE]
lyko trace --frames N [--gpu] [--output FILE]
lyko --help | --version
```

Scene render/measurement options: `--width N --height N --warmup N --measure-frames N`. Every option takes a value and may appear once. Paths accept spaces/Unicode. Parent output directories must exist. Output files are replaced when explicitly selected.

Build defaults: Windows configure `windows`, build/test `windows-debug`; Linux `linux-debug`. Source defaults to the current directory. Build configures then builds; test invokes CTest. Tool overrides are individual executable paths, never shell fragments.

Plain `run` launches the sandbox beside lyko (or the recorded build-tree path), waits and returns its exit code. `trace` launches that sandbox with `--frames` and `--trace`; it records CPU loop zones, not GPU events. Frame count is 1–1,000,000; default trace output is `lykoware-cpu-trace.json` in the working directory. `trace --gpu` instead runs the sandbox's Vulkan window without sound (`--window --vulkan --no-audio`) for N frames: each frame's CPU zones and, where the GPU has timestamps, its GPU time on a second track (ADR 0039). Its default output is `lykoware-gpu-trace.json`; it exits 3 when no window or Vulkan device is available.

`run --scene` parses versioned scene JSON, constructs an ECS/physics world, dispatches optional versioned commands before their fixed 60 Hz ticks, and runs 120 ticks by default. It works without a window or Vulkan when capture/measurement is not requested. `--report` records tick/hash, applied commands, entity count and measured CPU times. Scene and command parsing/dispatch use the public engine API in `lykoware/scene.hpp`; future scripting/editor callers can invoke that same handler. Build/test process routing is separate from the scene command contract.

`screenshot` or `--capture` creates an offscreen depth target, draws the scene through the real Vulkan renderer, and writes PPM P6 RGB bytes. It loads validated `cli_lit.vert.spv` and `cli_lit.frag.spv` from the executable's `shaders` directory, independent of the working directory. This is distinct from the GPU-free image test pattern. Installed builds deploy both shaders.

`--measure-frames` runs additional fixed ticks and synchronous offscreen draws after the replay, with raw samples and p50/p95/p99/max in the report. Warmup defaults to 30 draws of the settled scene; it does not advance simulation. Final capture is after the additional measured frames. Readback/file writing and pipeline creation are outside measured frames. This fixture measures offscreen throughput and CPU simulation, not display pacing, pure GPU time or VRAM residency. See [PERFORMANCE](PERFORMANCE.md) for the local Windows runner and limits.

Limits: 16 MiB per scene/command input; 10,000 lifetime IDs/commands; 36,000 initial ticks; 10,000 measured frames; 1,000 warmup draws; dimensions at most 8192 and 16 megapixels total. Authored envelopes use `schema_version: 1` (ADR 0004). Inputs reject unknown fields/schema versions, duplicate keys/IDs/sequences, nonfinite numbers and invalid shapes. Commands beyond `--ticks` are rejected before simulation; commands with nonexistent/destroyed IDs fail explicitly. Persisted files never contain runtime handles or process-local type IDs.

Exit codes:

- 0: operation completed.
- 2: invalid input/options, I/O failure, or reported runtime failure (with `lyko:` reason).
- 77: capture/measurement requested but no suitable Vulkan loader/device; CTest reports Skipped.
- 127: sandbox/tool executable unavailable or launch failed.
- Other nonzero statuses from build/test/sandbox children propagate. The sandbox retains its own GPU-unavailable exit code 3.

No scene/capture path starts a network request, external AI call, editor or paid service. CLI handlers are synchronous on one thread; worlds/resources are owned until handler return. Process handles use RAII. Allocation exceptions are caught at the CLI boundary; scene attachment rolls back before propagation. A failed replay stops and preserves earlier successful command effects in memory; there is no transactional rollback of an entire stream. There is no undo, remote execution service, process-tree cancellation, generic scripting language or full editor command catalogue yet.
