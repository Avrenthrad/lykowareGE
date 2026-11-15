# ADR 0099 — Distance fog in the post pass

Status: Proposed.
Related task/PR: `07.05e`, the last slice of `07.05`, branch `claude/07.05e-fog`.
Supersedes / superseded by: none. Builds on ADR 0087 (the HDR target and the post pass), ADR 0098 (the sky) and ADR 0086 (surfaces that do not write depth).

## Context

- **What exists:** far terrain is as crisp as near terrain, so an outdoor scene has no sense of depth and the horizon is a hard edge against the sky.
- **Constraints:**
  - ADR 0098 split fog out because fog in each scene shader needs the fog's colour and density there, and the lit block is full at 128 bytes. That would change every pipeline: lit, shadowed, material, transparent, water.
  - With fog off, images must not change, byte for byte.
  - The sky has no distance and must not be fogged.
  - The renderer already draws the scene into a float target with a depth buffer when post-processing is on, then makes one more pass over the image.

## Options

- **Fog in every scene shader.** Exact for transparent surfaces. Needs a wider block or a new uniform set in five shaders and all their pipelines.
- **Fog in the post pass, from the scene's depth.** One shader. Distance is recovered from the depth buffer the scene already wrote. Needs the depth to be kept and readable, and needs post-processing on.
- **Height or volumetric fog.** The right look for valleys and light shafts. Much more shader, and nothing needs it yet.

## Decision

- **Fog is part of the post pass.** `Renderer::enable_fog(FogShaders, Fog)` builds one pipeline from `engine/shaders/fog.slang`, used in place of the tone-map pipeline while fog is on. It reads the scene's colour and its depth, fogs, then tone-maps exactly as `post.slang` does.
- **The formula.** For a pixel showing a surface d metres from the camera's eye, in linear colour before exposure: `mix(scene, colour, min(max_opacity, 1 − exp(−density × max(d − start, 0))))`.
  - d is measured from the eye along that pixel's own view direction, not along the view axis, so the fog on a surface does not change when the camera turns.
  - Depth is reversed and infinite (`math::perspective`): view depth is `near / depth`, and d is that times the length of `(ndc.x × tan(fov_x / 2), ndc.y × tan(fov_y / 2), 1)`. It does not depend on where the camera is (ADR 0021).
- **No depth, no fog.** A pixel whose depth is 0 has no surface: the sky and the clear colour are left as they are. Set `Fog::colour` to the sky's horizon colour and far terrain fades into it.
- **Surfaces that do not write depth.** A transparent or water surface (ADR 0086, ADR 0097) takes the fog of whatever is behind it, and none with nothing behind it. That is the cost of doing fog in one pass.
- **The RHI.** `rhi::HdrTarget` now keeps its depth after each `render()` and exposes it as a second descriptor set, sampled without filtering. `GraphicsPipelineDesc::scene_depth` makes set 1 that depth, taken from the `HdrTarget` a mesh names as `MeshDraw::source`. No scene pipeline changes.
- **Who owns what.** The `HdrTarget` owns its depth image, sampler and set. The renderer owns the fog pipeline; `disable_fog()` keeps it alive, because a swapchain frame may still use it.
- **Needs post-processing.** `enable_fog` is refused while post-processing is off, and `disable_post()` turns fog off with it.
- **Failure.** `enable_fog` returns false and leaves the fog as it was for: post-processing off, a colour channel below 0 or not finite, a density outside 0..`fog_max_density`, a start outside 0..`fog_max_start`, a `max_opacity` outside 0..1 (all `InvalidArgument`), or shaders that do not load or link. Without the shaders there is no fog and the tone map runs alone. That is what a missing shader does. A device that cannot sample its depth format cannot make an `HdrTarget` (`NoSuitableDevice`), as for one that cannot sample float colour.

## Contracts and compatibility

- New: `render::Fog`, `render::FogShaders`, `fog_max_density`, `fog_max_start`, `Renderer::enable_fog`, `disable_fog`, `fog`; `engine/shaders/fog.slang`; `rhi::GraphicsPipelineDesc::scene_depth`, `GraphicsPipeline::scene_depth`, `HdrTarget::native_depth_descriptor_set`.
- Defaults keep every existing draw as it was: fog is off, and a pipeline without `scene_depth` is laid out as before.
- An `HdrTarget` now stores its depth instead of discarding it. Images are unchanged.
- `DrawStats` and `last_draw_count()` are unchanged by fog.
- Threading: the Device's thread, as the renderer today.
- Scene files do not store fog yet. That is GPT's format.

## Consequences

- Outdoor scenes have depth: terrain, foliage and far objects fade towards the horizon colour.
- Fog needs post-processing, so a skinned renderer has none yet (ADR 0087's later slice).
- Fog over water is that of the bed under it. Height fog, fog lit by the sun, and fog on transparent surfaces are later work.

## Verification

- `render.fog` (CI on the five hosted jobs): a wall 20 m ahead under flat lighting matches the formula within 2/255 at every eighth pixel, with the corners foggier than the centre; so do walls at 5 m and 60 m, a fog starting at 15 m, and a tilted view 1000 m from the origin. The same check on the image without fog is off by more than 40. A wall nearer than `start` and a density of 0 give the tone-mapped image byte for byte. Thick fog capped at 0.5 is an even mix. The clear colour and the sky are untouched. Fog is not a counted draw. Disabling fog gives the tone-mapped image, and disabling post-processing the first image, byte for byte. Refused: fog without post-processing, missing shaders, a NaN or negative colour, a density below 0 or past the limit, a negative or infinite start, a `max_opacity` of 1.5, and a scene-depth pipeline without the texture set.
