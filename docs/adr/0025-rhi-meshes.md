# ADR 0025 — Meshes in the RHI: buffers, depth, culling and push constants

Status: Proposed.
Related task/PR: 3D rendering, PR 2 (branch `claude/rhi-mesh`), stacked on PR 1 (maths, [ADR 0024](0024-math.md)).
Supersedes / superseded by: none. Extends [ADR 0011](0011-offscreen-render-target.md) (render target) and [ADR 0015](0015-graphics-pipeline.md) (pipelines).

## Context

The RHI could only draw vertices made up inside a shader. The owner's first 3D milestone is lit falling crates drawn from the ECS, which needs:

- mesh data on the GPU;
- a depth buffer so near objects hide far ones;
- back-face culling;
- per-object data (matrices and colour) for the shaders.

Precision across a 16 km world matters: positions reach the GPU camera-relative, and depth is reversed (ADR 0024).

## Options

- **Per-object data:**
  - push constants: 128 bytes guaranteed on every device, no descriptor sets, ideal for one matrix set per object;
  - uniform or storage buffers with descriptors: more data per object and better for thousands of objects, but much more API.

  Push constants were chosen now; buffers come with instancing when measurements ask for it.
- **Vertex formats:** a general vertex-layout description, or one fixed layout now. One fixed layout was chosen: position and normal, 24 bytes. More layouts come with textures and skinning.
- **Buffer memory:** staged uploads into device-only memory are fastest for the GPU but need a transfer path. Host-writable memory (device-local when the driver offers it) was chosen as the simplest correct start. Staging comes with asset streaming.

## Decision

Proposed:

- **`rhi::Buffer`** (`lykoware/rhi/buffer.hpp`): vertex or 32-bit index data, filled once at creation, up to 256 MiB.
  - For index buffers the largest index is recorded at creation, so draws can be checked against the vertex count.
- **Pipelines** (`GraphicsPipelineDesc`) gain:
  - `vertex_layout`: `None` or `PositionNormal` (float3 position at location 0, float3 normal at location 1, stride 24);
  - `depth`: test and write a 32-bit float depth buffer with reversed depth. The buffer clears to 0 (far) and a fragment passes when its depth is greater;
  - `cull_back_faces`: front faces are counter-clockwise as seen by the camera;
  - `push_constant_bytes`: 0 to 128, a multiple of 4, visible to both stages.
- **Render targets:** `RenderTargetDesc::depth` adds a depth buffer. `draw_meshes(clear, pipeline, meshes)` draws a list of indexed meshes, each with its own push constants, in one pass.
- **Misuse is refused before anything is submitted** (`InvalidArgument`):
  - a pipeline of another colour format, without a vertex layout, or with depth on a target without depth;
  - a missing or wrong-usage buffer;
  - an index count that is zero, not whole triangles, or more than the buffer holds;
  - an index past the last vertex;
  - push constants of the wrong size;
  - empty or oversized buffers, or index data that is not whole 32-bit indices.
- **Winding convention, established by test:** Vulkan judges winding in framebuffer space as the image is seen. `math::perspective` flips Y so the image is upright rather than mirrored, so counter-clockwise as seen by the camera stays counter-clockwise. My first draft of the pipeline said the opposite; I corrected it before the first test run, and `rhi.mesh` now locks it.
- **Shaders read matrices as four float4 columns** and multiply by hand (see `tests/shaders/lit.slang`), so there is no row-major or column-major ambiguity between C++ and Slang.

## Contracts and compatibility

- New public header `lykoware/rhi/buffer.hpp`. `pipeline.hpp` and `render_target.hpp` gain fields and functions with defaults that keep existing callers unchanged.
- `GraphicsPipeline::native_layout()` exposes the `VkPipelineLayout` to other RHI code, like `native_handle()`.
- Not yet:
  - the swapchain (window) path with depth: the next PR, with the falling crates;
  - staged uploads, textures, descriptor sets, instancing and blending.

## Consequences

- The RHI can draw lit 3D meshes with correct occlusion offscreen, which the CI pixel checks can test on lavapipe.
- 128 bytes of push constants per object covers a matrix, a normal rotation and a colour. Larger per-object data needs the buffer path above.

## Verification

- **New test `rhi.mesh`** (lavapipe in Linux CI; skipped where there is no Vulkan):
  - **Lighting:** a unit cube seen from the front shows its front face, lit exactly as the shader's lighting predicts for its normal (within 3 of 255).
  - **Seen from (2, 2, 2):** the top, right and front faces each have their own lighting. The top face is higher in the image and +X is to the right.
  - **Depth:** a near cube hides a larger far cube whichever is drawn first, while the far cube shows around it.
  - **Winding:** a face counter-clockwise as seen by the camera is drawn; the same face wound clockwise is culled.
  - **Repeatability:** drawing twice gives identical bytes.
  - **16 km out:** the first scene moved 16 km away gives exactly the same bytes, thanks to camera-relative maths.
  - **Misuse:** every refusal listed above.
  - No Vulkan validation errors.
- **Deliberate breaks, each caught:**
  - non-reversed depth compare;
  - depth test off;
  - clockwise front faces;
  - culling off;
  - unchecked index range;
  - push constants not sent.
- **A test bug found and fixed:** one misuse check first built a pipeline without vertex input from the mesh shader, which Vulkan's validator flagged. That check now uses the triangle shaders, which make their own vertices.
- **Sanitizers:** under ASan, LeakSanitizer first reported 112 bytes held by a lavapipe driver thread. This is the known false report that the other GPU tests avoid by keeping Vulkan drivers loaded (ADR 0015), so `rhi.mesh` joined that list. A deliberate 64-byte leak in the test's own code is still reported, at the right line.
- **Local results:** 37/37 pass in the Debug, ASan/UBSan and clang-tidy presets.
- **CI:** pending.
