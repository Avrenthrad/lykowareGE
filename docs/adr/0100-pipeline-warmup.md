# ADR 0100 — Pipeline warm-up and a compile counter

Status: Proposed.
Related task/PR: `06.10` (the current-scene half), branch `claude/06.10-pipeline-warmup`.
Supersedes / superseded by: none. Builds on ADR 0087 (the float-target copies of the scene pipelines) and ADR 0088 (counters the renderer reports).

## Context

- **What exists:** a driver compiles a pipeline's shaders when the pipeline is created. That can take milliseconds to hundreds of milliseconds, and inside a frame it is a visible hitch.
- **Where the renderer compiles today:**
  - `Renderer::create` and each `enable_*` compile their own pipelines at the call. Those are load-time.
  - With post-processing on (ADR 0087), each scene pipeline has a float-target copy. Enabling shadows, materials, transparency, water or the sky marks the copies stale, and they are rebuilt **inside the next `draw()`**. That is the one lazy path, and it lands in the first frame after a change.
- **Constraints:**
  - Nothing reports whether a pipeline compiled inside a span of frames, so a hitch test has nothing to assert on.
  - The world-scale hitch number needs streaming (GPT's `06.04`) and the owner's PC (`01.10`). This ADR does not claim it.

## Options

- **Time the frames and look for spikes.** What the target-PC run will do. On hosted CI and software GPUs frame times are noise, so it cannot gate.
- **Count compilations.** Exact and deterministic: either a pipeline was compiled inside the window or it was not.
- **A persistent pipeline cache on disk (`VkPipelineCache`).** Shortens the compile on later runs. It does not move it out of the frame, and it adds a file format. Later work.

## Decision

- **A counter.** `rhi::pipeline_compile_count()` is the number of graphics pipelines successfully created in this process. It only rises, and is safe to read from any thread. A caller reads it before and after a span of frames; a difference means a pipeline compiled inside the span.
- **A warm-up.** `Renderer::warm_up(error)` compiles now everything the next `draw()` would otherwise compile: today, the stale float-target copies. `Renderer::needs_warm_up()` says whether there is any such work. A loader calls `warm_up` after its last `enable_*`.
- **`draw()` is unchanged.** A caller that never warms up draws exactly as before, with the compile in the first frame.
- **Failure.** `warm_up` returns false with the error filled when a pipeline does not build, as `enable_post` does; the next `draw()` fails the same way. With nothing to compile it returns true and compiles nothing.
- **Who owns what.** The counter is process-wide, like `rhi::live_buffer_count()`. The pipelines stay owned by the renderer.
- **The harness column.** `pipeline_compiles`: the counter's difference across the measured frames, expected 0. Grok's harness (`tools/validation/`) records it from a fixture's output; this change does not edit that directory.

## Contracts and compatibility

- New: `rhi::pipeline_compile_count`, `Renderer::warm_up`, `Renderer::needs_warm_up`.
- No existing call changes behaviour. Images are byte-identical with or without a warm-up.
- Threading: `warm_up` on the Device's thread, as the renderer today; the counter from any thread.

## Consequences

- A fixture can prove that its measured frames compile nothing, on any machine, without timing.
- Not covered: creating the float target itself (an allocation at the first draw at each size, not a compile), texture and mesh uploads, pipelines made by code outside the renderer, and anything streaming will load. The world-scale half of `06.10` stays open.

## Verification

- `render.warmup` (CI on the five hosted jobs): with post-processing on and shadows and transparency enabled after it, a renderer that is not warmed compiles more than 0 pipelines in its first frame, which is the failure the gate exists to catch. A warmed renderer compiles them in `warm_up`, none in a second `warm_up`, and none across 60 frames of a moving scene and camera that use the lit, shadowed and blended paths. Its first frame equals the unwarmed renderer's, byte for byte. Enabling a feature again makes `needs_warm_up()` true until the next warm-up. A renderer without post-processing has nothing to warm.
