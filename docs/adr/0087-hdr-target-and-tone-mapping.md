# ADR 0087 — An HDR scene target and tone mapping

Status: Proposed.
Related task/PR: `07.03e`, the last slice of `07.03`, branch `claude/07.03e-post`.
Supersedes / superseded by: none. Builds on ADR 0082 (light), ADR 0084 (shadows), ADR 0085 (materials) and ADR 0086 (transparency, per-draw pipelines).

## Context

- **What exists:** the scene draws straight into an 8-bit target. Lit colours above 1 (a bright base colour, a material highlight) clip, so two different brightnesses look the same.
- **Constraints:**
  - A post pass needs the scene in a target it can sample, with range above 1.
  - Every scene pipeline is built for the final colour format. A float target needs the same pipelines built for another format.
  - With post-processing off, the image must not change, byte for byte.

## Options

- **Tone map inside each scene shader.** No new target, but blending then happens after the tone map, so transparent surfaces blend wrongly, and later post effects (bloom, grading) have nothing to read.
- **A 16-bit float scene target and a full-screen pass.** One more image and pass per frame. Blending happens in linear float, before the tone map.
- **An 11-11-10 float target.** Smaller, but no alpha channel and less precision; 16-bit float is the safe first choice.

## Decision

- **RHI: `HdrTarget`.** A 16-bit float RGBA image (`ColorFormat::Rgba16Float`) with a 32-bit depth buffer, a linear clamped sampler and one set with the texture layout. `render()` is one pass, as `RenderTarget::draw_meshes`, and leaves the colour ready to sample; it waits for the GPU, as `ShadowMap::render` does. A new target is cleared to black. A pipeline for another format is refused, and so is a skinned one (a later slice). A device that cannot render, blend and sample the format is `NoSuitableDevice`.
- **RHI: `MeshDraw::source`.** A draw may read an `HdrTarget` at set 0 instead of a `Texture`. A texturing pipeline needs exactly one of them.
- **Opt in per renderer.** `Renderer::enable_post(PostShaders, PostProcess)` builds the tone-mapping pipeline from `engine/shaders/post.slang` and one full-screen triangle. While on, `draw()` renders the scene into an `HdrTarget` sized to the final image (made again when the size changes), then draws the triangle into the final image. `disable_post()` returns to drawing directly. That is what a missing shader does.
- **Tone map.** Reinhard per channel: `e c / (1 + e c)`, with `e` the exposure (above 0, at most 64). A first operator, chosen because it is exact to check; filmic curves are later work.
- **Scene pipelines in float.** The renderer keeps each scene pipeline's SPIR-V and description (a recipe) when it is made: lit, shadow receiver, the two material pipelines and the transparent one. When post-processing is on, it builds float copies from those recipes, and builds them again whenever shadows, materials or transparency are enabled later. The shadow caster is depth-only and needs no copy.
- **Who owns what.** The renderer owns the HDR target, the float pipelines, the post pipeline and the triangle's buffers. All are freed with the renderer. `disable_post()` keeps them, because a swapchain frame may still use them.
- **Failure.** A refused exposure or shader changes nothing. If the float pipelines cannot be built, post-processing is left off. A skinned renderer refuses post-processing.

## Contracts and compatibility

- New: `rhi::ColorFormat::Rgba16Float`, `rhi::HdrTarget`, `MeshDraw::source`; `render::PostProcess`, `PostShaders`, `post_max_exposure`, `Renderer::enable_post`, `disable_post`, `post`.
- Existing pipelines, draws and targets are unchanged when these are unused. `record_mesh_pass` now accepts a null pipeline for a clear-only pass.
- Threading: the Device's thread.

## Consequences

- Highlights and bright colours keep their differences, and transparency blends in linear float before the tone map. Later post passes (bloom, colour grading, exposure adaptation) can read the same target.
- Each frame with post-processing waits for the scene pass before the final pass, as shadows do. Folding the passes into one submission is `07.04` or later work.
- Skinned meshes with post-processing are a later slice.

## Verification

- `render.post` (lavapipe locally; CI on the five hosted jobs): three cubes lit to linear 0.5, 2 and 4 before a white wall. Off, 2 and 4 both clip to 255. On, every value matches the Reinhard formula within 2/255 at exposures 1 and 2 (lavapipe: exact), and 2 and 4 stay more than 20 apart. A half-transparent pane enabled after post-processing blends in float, then tone-maps, within 2/255; plain parts through the material pipeline keep values above 1 with shadows on. Disabled again is byte-identical to off. Refused: exposures 0, −1, 100 and NaN (settings unchanged), a missing shader, a skinned renderer, an empty HDR target and an 8-bit pipeline on the HDR target.
- Deliberate break: clamping the sampled scene to 1 in `post.slang` (what an 8-bit scene target would do) fails the linear 2 and 4 checks and "values above 1 stay apart".
