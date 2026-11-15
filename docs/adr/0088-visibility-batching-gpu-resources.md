# ADR 0088 — Visibility, batching and GPU resources

Status: Proposed.
Related task/PR: `07.04`, branch `claude/07.04-visibility-batching`.
Supersedes / superseded by: none. Builds on ADR 0026 (the first renderer), ADR 0030 (renderer-owned ids), ADR 0035 and ADR 0037 (added meshes and textures), ADR 0084 (shadows) and ADR 0086 (per-draw pipelines and transparent order).

## Context

- **What exists:** `Renderer::draw` issues one indexed draw per mesh part, for every entity, in ECS order. Added meshes and textures stay until the renderer is destroyed; nothing can free one. Nothing counts GPU objects, so a leak passes every test.
- **Constraints:**
  - A renderer that is never given the new settings must draw exactly as before, byte for byte, and keep its ECS order.
  - Counts must come from the renderer, not from a log a test scrapes.
  - No new dependency. The RHI stays one Vulkan path on the Device's thread.

## Options

- **Culling:** bounding spheres against the view (cheap, conservative) / boxes (tighter, more work per entity) / GPU occlusion queries (needs readback and a frame of latency). Spheres first.
- **Batching:** sort draws by state so neighbours share bindings, and skip redundant binds (no shader change) / GPU instancing (one draw for many entities; needs a per-instance buffer and new shader inputs) / merging static geometry (rebuilds buffers when anything moves). State batching first; instancing can follow on the same sort key.
- **Freeing:** a deferred free list fenced per frame / wait for the device and free at once. Waiting is simple and correct for load-time and level changes, which is where freeing happens today.

## Decision

- **`render::Visibility { cull, batch }`, both off by default**, set by `Renderer::set_visibility`.
- **What is culled.** With `cull`, an entity is skipped when its mesh's bounding sphere, placed by its Transform and scaled by the largest `|MeshRenderer::scale|` component, lies wholly behind the near plane or past one of the four side planes. There is no far plane (reversed depth reaches infinity). The sphere is the box around the mesh's vertices, computed at upload, made 0.01 % larger so rounding never culls a visible vertex. A skinned entity is never culled, since its pose may leave the bind-pose bounds. A culled entity still casts its shadow: something outside the view can shadow something inside it. A culled entity's material and textures are not checked that frame.
- **What is batched.** With `batch`, opaque parts are stably sorted by mesh id, then base texture id, then normal-map id, then metal-roughness id (the cube is mesh 0, no texture is 0). Ids, not pointers, so the order is the same on every run. Transparent parts keep their far-to-near order (ADR 0086). A batch is a run of consecutive draws sharing pipeline, vertex and index buffers, base, normal and metal-roughness textures, with no skin pose. The RHI now binds vertex and index buffers only when they change, as it already did for descriptor sets, so a batch binds its state once.
- **Counts.** `Renderer::last_draw_stats()` returns `DrawStats { entities, culled, draws, batches }` from the last draw.
- **How a GPU resource is created and freed.** The renderer owns every buffer and texture it creates. `remove_mesh(MeshId)` and `remove_texture(TextureId)` wait for the device to go idle (`Device::wait_idle`), then destroy the buffers or texture at once, so no frame in flight reads freed memory. A freed id draws nothing, as a foreign id does, and is never handed out again. `gpu_resources()` reports the added meshes, textures and mesh bytes still held.
- **Failure.** `remove_*` returns false, fills the error and frees nothing for: an id the renderer did not make or already freed (InvalidArgument), a texture still named by a live mesh part or part material (InvalidArgument, so a part never points at freed memory), or a failed wait (as `Device::wait_idle`). An entity-level material naming a freed texture fails `draw()` as a foreign texture does today (ADR 0085).
- **Leaks.** `rhi::live_buffer_count()` and `rhi::live_texture_count()` count every Buffer and Texture alive in the process, from construction to destruction. A test that frees what it made must see them return to where they started. A leaked resource fails that check.

## Contracts and compatibility

- New API: `Visibility`, `DrawStats`, `GpuResources`, `Renderer::set_visibility`, `visibility`, `last_draw_stats`, `remove_mesh`, `remove_texture`, `gpu_resources`; `rhi::live_buffer_count`, `rhi::live_texture_count`.
- Unchanged by default: with `Visibility{}`, draw order, images and `last_draw_count()` are as before. Skipping redundant buffer binds does not change what is drawn.
- With `batch`, opaque draw order changes. Two opaque surfaces at exactly equal depth may then resolve the other way; nothing else in the image changes.
- Threading: the Device's thread, as the renderer. The live counters are atomic and may be read anywhere.
- Ids stay process-local (ADR 0030). Freeing never renumbers.

## Consequences

- Culling and batching are opt-in for now. The sandbox and the flagship should turn both on; the sandbox change is a separate, small PR.
- `remove_*` stalls the GPU. It is meant for load and unload, not per frame. A fenced free list replaces the wait when streaming (GPT `06.04`) frees chunks every frame.
- Deferred: GPU instancing (one draw for a batch of one mesh), occlusion culling, culling skinned entities with pose bounds, shadow-caster culling against the light's box, and per-device rather than per-process counters.

## Verification

- `render.visibility` (lavapipe in CI; skips without Vulkan):
  - Twelve boxes interleaving two meshes and two textures: twelve batches unbatched, four batched, twelve draws either way, byte-identical image, the same order on a second draw, and twelve again once batching is off.
  - Culling: a cube behind the camera and one far to the side are culled; a cube whose centre is past the right edge but whose side shows, and a cube behind the eye scaled to hold the camera, are not. The culled image is byte-identical to the unculled one. With shadows on, all sixteen entities still cast.
  - Resources: two meshes and two textures add four buffers and two textures to the live counts; refusals for a named texture, a foreign id, an empty id and a second free; freed ids draw nothing; freeing returns the counts to the renderer's own; destroying the renderer returns them to the start; a new mesh gets a new id. Deliberate break: a buffer still held shows up as a leak.
- Existing `render.*` tests run unchanged with visibility off.
