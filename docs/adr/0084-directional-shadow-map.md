# ADR 0084 — A directional light's shadow map

Status: Proposed.
Related task/PR: `07.03b`, the second slice of `07.03`, branch `claude/07.03b-shadows`.
Supersedes / superseded by: none. Builds on ADR 0082 (the stated light), ADR 0026 (the first renderer) and ADR 0021 (camera-relative drawing).

## Context

- **What exists:** one directional light and an ambient (ADR 0082). Nothing casts a shadow.
- **Constraints:**
  - The lit push-constant block is full at 128 bytes, the guaranteed Vulkan minimum. A per-draw light matrix does not fit.
  - The RHI has no sampled depth image and no plain uniform buffer.
  - The unshadowed image must not change, byte for byte.
  - Skinned renderers use set 1 for the joint palette (ADR 0052).

## Options

- **A per-draw light matrix in the push constants.** Needs 192 bytes, past the guaranteed minimum.
- **A per-frame matrix in a uniform block, applied to the camera clip position.** Camera clip space to light clip space is one linear map per frame: `light_view_projection × view⁻¹ × projection⁻¹`. The vertex shader applies it to its own clip position, so per-draw data stays 128 bytes.

The second. The block sits with the shadow map in one descriptor set, so there is one new GPU object, not a general uniform-buffer system. That stays with `07.04`.

## Decision

- **RHI:** `rhi::ShadowMap` in `rhi/shadow_map.hpp`.
  - It owns a square D32 depth image (16..8192 texels), a nearest clamp sampler, an 80-byte uniform block (`ShadowBlock`) and one descriptor set.
  - `render(casters, meshes, block)` clears, draws the casters, stores the block with `vkCmdUpdateBuffer`, and moves the image to shader-read. It waits for the GPU, as `RenderTarget` does. It orders itself after earlier frames that read the map, so a swapchain frame in flight is safe.
  - A new map is cleared, so a receiver drawn before any `render()` is lit.
- **Pipelines:** `GraphicsPipelineDesc::depth_only` (no fragment shader, no colour attachment) for casters. `GraphicsPipelineDesc::shadow` puts the shadow set at set 1 for receivers. `MeshDraw::shadow` names the map; it is required exactly when the pipeline receives a shadow.
- **Shaders:** `engine/shaders/shadow.slang` is the caster: the light's clip × model in a 64-byte push block. `lit.slang` gains `vertexShadowed` and `fragmentShadowed`. `vertexMain` and `fragmentMain` are unchanged.
- **The light's projection:** orthographic and camera-relative (ADR 0021).
  - A square of half side `half_extent` around the camera's target, `depth_range` deep along the light and centred on the target.
  - Reversed depth as everywhere in the RHI. The map clears to 0, which means nothing casts.
- **The receiver:** the diffuse share is scaled by the light reaching the point: nine taps (3 × 3 texels), each lit unless the stored depth is past the point's depth plus `bias`. Outside the square, behind the light or past its far plane, a point is lit. Ambient is never shadowed.
- **API:** `render::Shadows { resolution, half_extent, depth_range, bias }`, `render::ShadowShaders`, `Renderer::enable_shadows`, `disable_shadows`, `shadows`, `last_shadow_caster_count`.
  - Off by default. With shadows off the renderer draws exactly as before.
  - Every drawn entity casts, with all its parts. There is no per-entity opt-out yet.
- **Ownership and threading:** the renderer owns the map and both pipelines, and frees them with itself or on the next `enable_shadows`. One thread, the Device's, as ADR 0026.
- **Refusals:** `enable_shadows` returns false and leaves shadows as they were for:
  - a renderer made with the skinned vertex shader (skinned casters and receivers are a later slice);
  - a resolution outside 16..8192 or past the device limit;
  - an extent or range that is not finite and positive, or a bias outside 0..range;
  - a shader that does not load or link. A missing or empty SPIR-V is refused the same way as in `Renderer::create`.
- **A stale shader:** SPIR-V built before this change has no `vertexShadowed`, so the build fails to compile it. A receiver that ignores the map is caught by `render.shadows`, because the ground under the cube must darken.

## Consequences

- The sun casts shadows over a square around the camera's target. Cascades, texel snapping against shimmer, and per-entity casting flags are later work.
- A shadowed frame waits for the GPU once more. Folding the shadow pass into the frame's own submission belongs with `07.04`.
- Skinned characters neither cast nor receive yet. That needs a skinned caster and a set layout beside the palette.

## Validation

`render.shadows` draws on lavapipe. It skips with exit code 77 when there is no Vulkan; it never passes without drawing.
- **Shadowed:** a 1 m cube floats over a ground slab under a light from above. The ground under it keeps only the ambient, within 2/255. The open ground and the cube's top stay fully lit, so there is no self-shadowing.
- **Deliberate breaks:** without shadows the spot under the cube is fully lit; with the cube removed it is lit again; and a light along (1, 1, 0) moves the shadow to x −2.5..−0.5 and lights the spot under the cube.
- **Unchanged when off:** with shadows disabled again, the image is byte for byte the image from before they were enabled.
- **Refusals:** each bad setting above, a missing caster shader and a skinned renderer are refused, and shadows stay off.
