# ADR 0097 — Water surfaces that mirror at glancing angles

Status: Proposed.
Related task/PR: `07.05c`, the third slice of `07.05`, branch `claude/07.05c-water`.
Supersedes / superseded by: none. Builds on ADR 0086 (transparent surfaces and per-draw pipelines), ADR 0082 (the stated light), ADR 0087 (the HDR target) and ADR 0021 (camera-relative drawing).

## Context

- **What exists:** a transparent surface has one opacity from every angle (ADR 0086). Water does not look like that: looking down you see into it, and towards the horizon it mirrors the sky and hides what is under it.
- **Constraints:**
  - No reflection or refraction passes exist, and the renderer has no clock, so nothing here may need a second view of the scene or the time.
  - With water off, or the component unset, images must not change, byte for byte.
  - The result must be a formula a test can compute.

## Options

- **Use a transparent surface and call it water.** Nothing new, and it looks like tinted glass.
- **A water pipeline with an angle-dependent mirror of one stated colour.** One shader, one pipeline, drawn with the transparent surfaces. Cheap, testable, and the place later slices add waves and real reflections.
- **Planar reflection and refraction with a depth-based tint.** Looks best, but needs extra scene passes and a sampled depth buffer.

## Decision

- **A component and an opt-in pipeline.** `MeshRenderer::water` (`render::Water { reflection }`) marks an entity as water. `Renderer::enable_water(WaterShaders)` builds one pipeline from `engine/shaders/water.slang`: blended, depth tested and not written, back faces culled, a 192-byte block. `disable_water()` returns to ignoring the component. Without the shaders the component is ignored. That is what a missing shader does.
- **The formula.** With N the surface normal and V the unit direction from the surface to the camera:
  - `F = 0.02 + 0.98 × (1 − max(N·V, 0))⁵` (Schlick's approximation; 2% is water seen head-on).
  - colour = the lit body colour mixed towards `Water::reflection` by F. The body colour is lit as the lit shader lights it: colour × texture × (ambient + (1 − ambient) × max(0, N·L)).
  - alpha = `MeshRenderer::opacity` × the texture's alpha, mixed towards 1 by F.
  - The blend is the transparent one: colour × alpha + behind × (1 − alpha).
- **Order.** Water parts join the transparent parts: after every opaque draw, stably sorted far to near by the entity's distance from the eye. An entity with `water` set is drawn this way whatever its opacity, and whether or not transparency is enabled.
- **Who owns what.** The renderer owns the pipeline and its HDR copy (ADR 0087). `disable_water()` keeps the pipeline alive, because a swapchain frame may still use it; it is freed with the renderer or replaced by the next enable.
- **Failure.** `enable_water` returns false and leaves water as it was for a skinned renderer (`InvalidArgument`), shaders that do not load or link, or a device with fewer than 192 bytes of push constants (`NoSuitableDevice`). While water is on, `draw()` fails with `InvalidArgument` for a water entity whose opacity is outside 0..1 or not finite, or whose reflection has a channel below 0 or not finite.
- **What a water surface does not do in this slice.** It casts no shadow and receives none, and draws without its `Material`. It is seen from its front faces only, so not from under the surface. It does not move: waves need a clock the renderer does not have yet. It mirrors one colour, not the scene.

## Contracts and compatibility

- New: `render::Water`, `MeshRenderer::water`, `render::WaterShaders`, `Renderer::enable_water`, `disable_water`, `water_enabled`, `last_water_draw_count`; `engine/shaders/water.slang`.
- `last_transparent_draw_count()` now counts blended parts other than water. It is unchanged for every scene without water.
- Defaults keep every existing draw as it was: `water` is unset.
- Threading: the Device's thread, as the renderer today.
- Scene files do not store water yet. That is GPT's format.

## Consequences

- Lakes, rivers and pools read as water at any view angle with one draw each.
- The reflection colour is the caller's to keep in step with the sky (`07.05d`).
- Waves, a view from below, scene reflections, depth-based tint and shore foam are later slices.

## Verification

- `render.water` (lavapipe locally; CI on the five hosted jobs): a half-opaque pool over sand, seen from almost straight above and from almost along the surface. Each pixel matches the formula within 3/255, blended over the sand pixel read from the same view without the pool. With the pool's draw removed the pixel is more than 20/255 from the formula. The glancing view is bluer by more than 30. At opacity 1 nothing behind shows; a red cube under the water shows through. Without water shaders, and after `disable_water`, the image is byte-identical to the pool drawn as an ordinary box. With shadows on only the sand casts; with post-processing on the pool is still drawn. Refused: an opacity of 1.5, a NaN or negative reflection, missing shaders, a skinned renderer.
