# ADR 0086 — Transparent surfaces, sorted and blended

Status: Proposed.
Related task/PR: `07.03d`, the fourth slice of `07.03`, branch `claude/07.03d-transparency`.
Supersedes / superseded by: none. Builds on ADR 0082 (the stated light), ADR 0084 (shadows), ADR 0085 (materials) and ADR 0021 (camera-relative drawing).

## Context

- **What exists:** every surface is opaque. One render pass draws with one pipeline (`RenderTarget::draw_meshes`, `Swapchain::render_meshes`), and no pipeline blends.
- **Constraints:**
  - Transparent surfaces must draw after the opaque ones, far to near, with blending on and depth writes off. That is a second pipeline inside the same pass.
  - The lit push block is full at 128 bytes, so an opacity needs a wider block, as materials did (ADR 0085).
  - With transparency off, the image must not change, byte for byte.

## Options

- **A second render pass that loads the first pass's colour and depth.** Keeps one pipeline per pass, but the colour target must keep its depth between passes, and the swapchain frame loop would need a second pass too.
- **A per-draw pipeline inside one pass.** `MeshDraw::pipeline` names the pipeline for that draw; the pass binds it when it changes. Small, and `07.04`'s batching can sort by it later.
- **Order-independent transparency (weighted blended or per-pixel lists).** Needs extra targets and a resolve pass. More than this slice needs; sorted blending is the usual first step.

## Decision

- **RHI: a per-draw pipeline.** `MeshDraw::pipeline` (null means the pass's pipeline). It must target the same colour format, use depth exactly when the pass's pipeline does, and match its depth-only use; otherwise the draw is refused with `InvalidArgument` and nothing is submitted. Every other draw rule is checked against the draw's own pipeline. Switching pipelines forgets the bound descriptor sets, so a later draw rebinds them under its own layout.
- **RHI: blending and depth writes.** `GraphicsPipelineDesc::blend` (`BlendMode::None` or `Alpha`: straight alpha, colour `src × a + dst × (1 − a)`, alpha `a + dst.a × (1 − a)`) and `depth_write` (default true). A blended pipeline may use up to `push_constant_material_max_bytes`, as a material one may. A blended depth-only pipeline is refused.
- **Opt in per renderer.** `Renderer::enable_transparency(TransparencyShaders)` builds one pipeline from `engine/shaders/transparent.slang`: blended, depth tested, not written, back faces culled, a 144-byte block (the lit block plus the opacity). `disable_transparency()` returns to drawing everything opaque. Without the shaders, `MeshRenderer::opacity` is ignored. That is what a missing shader does.
- **Who owns what.** The renderer owns the pipeline. `disable_transparency()` keeps it alive, because a swapchain frame may still use it; it is freed with the renderer or replaced by the next enable.
- **Which entities.** `MeshRenderer::opacity`, 0..1, default 1. Below 1 while transparency is on, the entity is transparent; every part of it uses that opacity, times the base colour texture's alpha. A value outside 0..1 or not finite fails `draw()` with `InvalidArgument` while transparency is on.
- **Order.** Opaque entities draw first, in ECS order, with the pipeline they use today. Transparent parts follow, stably sorted by the distance from the camera's eye to the entity's position, far to near. Ties keep ECS order, and parts of one entity keep their order. Distance is computed in double precision from world positions, so it is as exact far from the origin as near it (ADR 0021).
- **What a transparent surface does not do in this slice.** It casts no shadow and receives none, and it draws without its `Material`: it is lit by the lit formula alone. Overlapping triangles inside one entity are not sorted. A skinned renderer refuses transparency (`InvalidArgument`), as it refuses shadows and materials.

## Contracts and compatibility

- New: `rhi::BlendMode`, `GraphicsPipelineDesc::blend` and `depth_write`, `GraphicsPipeline::blend()` and `depth_write()`, `MeshDraw::pipeline`; `render::TransparencyShaders`, `MeshRenderer::opacity`, `Renderer::enable_transparency`, `disable_transparency`, `transparency_enabled`, `last_transparent_draw_count`.
- Defaults keep every existing pipeline and draw as it was: no blending, depth written, the pass's pipeline.
- Threading: the Device's thread, as the renderer today.
- Scene files do not store opacity yet. That is GPT's format.

## Consequences

- Glass, water surfaces and fades can be drawn now. A shared pass with per-draw pipelines is what `07.04`'s batching will sort.
- Sorting is per entity. Intersecting transparent surfaces, or one large mesh around the camera, can still show order errors. Order-independent transparency is later work if a scene needs it.
- Transparent shadows, materials on transparent surfaces and skinned transparency are later slices.

## Verification

- `render.transparency` (lavapipe locally; CI on the five hosted jobs): two half-transparent panes, red behind blue, in front of a white wall, all fully lit. The overlap, red alone, blue alone and the bare wall each match a CPU blend within 2/255. Adding the panes near-first or far-first gives the same image, byte for byte; the unsorted order would differ by more than 60 in the overlap, and the test checks the fixture can tell. Opacity 1 with transparency on, the shaders absent, and transparency disabled again are each byte-identical to the opaque image. Opacity 0 leaves the wall exactly, and a pane behind the wall is hidden. With materials and shadows on, the overlap blends the same within 3/255 and only the wall casts. Refused: opacities 1.5, −0.1 and NaN; a missing shader; a skinned renderer; a blended depth-only pipeline; a draw whose own pipeline has another colour format or no depth in a depth pass.
