# ADR 0039 — GPU timings in traces

Status: Proposed.
Related task/PR: GPU timings (branch `claude/gpu-timings`), stacked on textures PR 3 ([ADR 0038](0038-gltf-materials.md)).
Supersedes / superseded by: none. Extends the CPU trace of [ADR 0006](0006-cpu-trace.md), the `lyko` CLI of [ADR 0005](0005-native-cli.md), and the render target and swapchain of [ADR 0011](0011-offscreen-render-target.md) and [ADR 0012](0012-swapchain.md).

## Context

Phase 1's exit asks that "traces report CPU/GPU timings where supported". Traces held CPU zones only, and nothing measured how long the GPU spent on a frame.

## Options

- **How to measure:**
  - **Vulkan timestamp queries** around each submission's commands;
  - **a vendor profiler or a tracing library** (Tracy, RenderDoc, Nsight).

  Timestamp queries were chosen: they are core Vulkan, need no new library, and work on lavapipe, so CI can test them. Profilers stay useful for deeper looks.
- **Where the GPU time goes:**
  - **into the Chrome trace** the CPU zones already use, on a second track;
  - **a separate file.**

  The same trace was chosen, so one file shows both.
- **Placing GPU spans on the CPU's clock:**
  - **at the CPU moment the frame was submitted**, with the GPU's measured duration;
  - **calibrated timestamps** (`VK_KHR_calibrated_timestamps`), which map GPU ticks to the CPU clock exactly.

  Submit time was chosen for now: it needs no extension. The GPU starts no earlier than the submit, so a span may sit slightly early. Calibrated timestamps can come later.

## Decision

Proposed:

- **`rhi::Device`:**
  - **`gpu_timestamps()`** says whether the graphics queue writes timestamps. That needs `timestampValidBits > 0` and a non-zero `timestampPeriod`.
  - Ticks are masked to the valid bits, so a counter that wraps still gives the right interval. They are scaled by the period to nanoseconds.
  - Results are read with their availability, never waited on. An unavailable result gives no time rather than a wrong one.
- **`rhi::RenderTarget::last_gpu_time_ns()`:** the GPU time of the last `clear`, `draw` or `draw_meshes`. It is timed between a top-of-pipe and a bottom-of-pipe timestamp around the recorded commands.
  - It is nullopt before the first, after a refused or failed one, and without timestamps.
  - `read_back` does not change it.
- **`rhi::Swapchain::take_gpu_frame_times()`:**
  - Each frame slot has two timestamp queries, written around the frame's commands.
  - A frame's time is read once its fence has signalled, either at the slot's next frame or in `take_gpu_frame_times()`, which checks the fences without waiting.
  - Each `GpuFrameTime` holds:
    - `frame`: its number among submitted frames;
    - `nanoseconds`;
    - `submitted`: the CPU's steady-clock time at submit.
  - Results come oldest first. At most 1024 are kept between calls.
  - A frame's time can include waiting for its swapchain image. The top-of-pipe timestamp may be written before the image-available semaphore lets drawing start.
- **`CpuTrace`:**
  - **`add_gpu_span(name, start_us, duration_us)`** stores a complete event with category `gpu` on tid 2;
  - **`elapsed_us()`** gives the session clock for placing them;
  - a document with GPU spans names tid 1 "CPU" and tid 2 "GPU" with `thread_name` metadata events;
  - a CPU-only document is byte-for-byte unchanged;
  - `completed_count()` counts zones and GPU spans.
- **Sandbox:**
  - **`--window --vulkan --frames N --trace FILE`** writes a trace. Each frame has `sandbox.frame` with `sandbox.simulate`, `sandbox.audio` and `sandbox.render` inside it, and one `gpu.frame` span at the frame's submit time.
  - **Every `--vulkan` run** logs the average GPU time per frame on exit, or says the device has no timestamps.
- **`lyko trace --gpu --frames N [--output FILE]`** runs the sandbox's Vulkan window without sound for N frames and writes that trace. The default file is `lykoware-gpu-trace.json`. It exits 3 when no window or Vulkan device is available, as the sandbox does.

## Contracts and compatibility

- **New:**
  - `Device::gpu_timestamps()`;
  - `RenderTarget::last_gpu_time_ns()`;
  - `rhi::GpuFrameTime` and `Swapchain::take_gpu_frame_times()`;
  - `CpuTrace::add_gpu_span()` and `CpuTrace::elapsed_us()`;
  - the sandbox's `--trace` with `--window`, and `lyko trace --gpu`.
- **Changed:**
  - `CpuTrace::completed_count()` now also counts GPU spans. Before, there were none.
  - `lyko trace --gpu` was refused as an unknown option, and is now accepted.
- **Device:** a Device with timestamps creates a two-query pool. A Swapchain on it creates one with two queries per frame in flight.
- **Cost:** every RenderTarget render and swapchain frame records a query reset and two timestamps. RenderTarget also reads two results after its wait.
- **Not yet:**
  - calibrated timestamps for exact placement;
  - per-pass or per-draw GPU zones;
  - pipeline statistics;
  - GPU timing of `Texture` uploads;
  - a perf budget on GPU time (the CPU budgets of ADR 0006 are unchanged).

## Consequences

- The Phase 1 exit item "traces report CPU/GPU timings where supported" is met for the sandbox's Vulkan path. A device without timestamps still gets a CPU-only trace and a log line that says so.
- The trace is still the hand-written Chrome trace JSON of ADR 0006, so `chrome://tracing` and Perfetto open it.

## Verification

- **`trace.api`**, new GPU-span case:
  - spans are `gpu` events on tid 2 at the given time and duration;
  - negative times are stored as zero;
  - the tracks are named;
  - CPU zones stay on tid 1;
  - zones and spans share one clock;
  - the CPU-only document is unchanged (existing cases).
- **`rhi.render_target`:**
  - no time before the first render;
  - a clear has a time exactly when the device has timestamps;
  - that time is no longer than the call that waited for it (plus 10 %);
  - `read_back` keeps it.
- **`rhi.mesh`:** a mesh draw has a plausible time, and a refused draw clears it.
- **`rhi.swapchain`:**
  - after the first five frames, and again after a resize and four triangle frames, each frame has one time;
  - frames are numbered in submission order;
  - submit times are ordered and past;
  - times are plausible, and none is returned twice;
  - a refused draw times nothing;
  - the validation layer reports no errors.
- **`sandbox.gpu_trace`** (new):
  - the sandbox's offscreen Vulkan window writes a trace with 5 `sandbox.frame` zones, the simulate and render zones, and 5 `gpu.frame` spans on the named GPU track;
  - it logs the average;
  - without timestamps it checks that there are no GPU spans.
- **`foundation.smoke`:** `--window --trace` without `--vulkan` or `--frames`, a missing value, and a duplicate are refused.
- **`cli.commands`:**
  - `trace --gpu` without `--frames`, and a duplicate `--gpu`, are refused;
  - `lyko trace --gpu --frames 2` exits 0 with two frames in the trace, or 3 without a display or Vulkan.
- **Deliberate breaks:** each caught.
- **Local results:** all tests pass in the Debug, ASan/UBSan and clang-tidy presets. lavapipe reports 64-bit timestamps, so GPU times are measured there.
- **CI:** blocked for the whole account since 2026-11-05 (see ADR 0033); to be recorded.
- **Owner check wanted:** `lyko trace --gpu --frames 300` on the RTX 4070 desktop. Open the trace in Perfetto (ui.perfetto.dev) and see the CPU and GPU tracks.
