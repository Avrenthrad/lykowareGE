# ADR 0085 — Metal-roughness materials and normal maps

Status: Proposed.
Related task/PR: `07.03c`, the third slice of `07.03`, branch `claude/07.03c-materials`.
Supersedes / superseded by: none. Builds on ADR 0082 (the stated light), ADR 0084 (the shadow map), ADR 0038 (mesh parts) and ADR 0021 (camera-relative drawing).

## Context

- **What exists:** a base colour and texture per part, one directional light and ambient (ADR 0082), and a shadow map (ADR 0084). Every surface is Lambert.
- **Constraints:**
  - The lit push-constant block is full at 128 bytes. Specular light needs the view direction, so each draw needs its camera-relative position, plus three material factors.
  - One render pass draws with one pipeline (`RenderTarget::draw_meshes`). A frame cannot mix the lit and material pipelines without a multi-pipeline pass, which is `07.04`'s batching work.
  - The image without materials must not change, byte for byte.
  - Meshes carry no tangents (`VertexLayout::PositionNormalUv`).

## Options

- **Per-material uniform blocks.** Needs a new RHI uniform object with frame-in-flight copies. More than this slice needs.
- **A 192-byte push block on a separate material pipeline.** The lit block, then the camera-relative model matrix and the factors. Vulkan guarantees only 128 bytes, but every desktop GPU of the Vulkan Roadmap 2022 profile, lavapipe and the RTX 4070 offer 256.
- **Vertex tangents.** A new vertex layout and importer change. Screen-space derivatives give the same frame on flat and smooth surfaces with no data change.

## Decision

- **Opt in per renderer.** `Renderer::enable_materials(MaterialShaders)` builds two pipelines from `engine/shaders/material.slang`: plain, and shadowed for when ADR 0084's shadows are also on. `disable_materials()` returns to the lit path. Without material shaders, a `Material` is ignored and the part draws with its base colour. That is what a missing shader does.
- **Who owns what.** The renderer owns both pipelines and a 1×1 linear flat normal map. Material textures are the renderer's own `TextureId`s (ADR 0037), so their GPU lifetime is the renderer's. `disable_materials()` keeps the pipelines alive, because a swapchain frame may still use them. They are freed with the renderer.
- **Where a material lives.** `MeshRenderer::material` for the entity, and `MeshPart::material` overriding it for one part (as with textures). A `Material` holds roughness and metalness (0..1), a normal scale (0..2), a normal map and a metal-roughness texture (green roughness, blue metalness, as in glTF). Scene files do not store materials yet.
- **One pipeline per frame.** While materials are on, every part draws with the material pipeline. A part without a material sets `material.w = 1`, and the shader then runs `lit.slang`'s Lambert formula exactly. So turning materials on changes nothing else.
- **Shading.** GGX distribution, Smith–Schlick geometry (k = α/2) and Schlick Fresnel, with F0 = mix(0.04, albedo, metalness). The diffuse part is (1 − F)(1 − metalness) × albedo. The light's radiance is π, so a white Lambert surface facing it still gives 1:
  `albedo × ambient + (1 − ambient) × (diffuse + π × specular) × n·l × shadow`. Roughness is clamped to at least 0.03.
- **Normal maps.** Tangent-space and linear (`Rgba8Unorm`). +x is right and +y is up in the image, which is −v. The tangent frame comes from screen-space derivatives of the camera-relative position and the texture coordinate. With no normal map, the renderer passes a normal scale of 0. The shader then uses the surface normal exactly, because 128/255 does not unpack to exactly 0.
- **RHI.** `GraphicsPipelineDesc::material`: set 1 is the ShadowMap layout either way, and sets 2 and 3 use the texture layout. `MeshDraw::normal_map` and `metal_roughness` are required exactly when the pipeline shades materials. Material pipelines may ask for up to `push_constant_material_max_bytes` (256). A device below the requested size refuses with `NoSuitableDevice`, and the test skips.
- **Failure.** `enable_materials` fails on a skinned renderer (InvalidArgument; skinned materials are a later slice), on shaders that do not load or link, or on a device without 192 bytes of push constants. Materials stay as they were. While materials are on, `draw()` fails with InvalidArgument for a factor outside its range or not finite, a texture another renderer made, or an sRGB normal map. `add_mesh` refuses such a part material at upload, whether or not materials are on.
- **Threading.** The Device's thread, as the rest of `Renderer`.

## Consequences

- Image-based lighting, emissive surfaces, occlusion maps and clear coat are not here. They need an environment (07.05) or more texture sets.
- `07.04` replaces "one pipeline per frame" with batches per pipeline and material. Then the lit pipeline can draw plain parts again, and the `material.w` branch goes.
- glTF import does not yet fill `MeshPart::material`. The fields match glTF's metal-roughness factors and textures, so wiring it in later is mechanical.

## Validation

`render.materials`:

- Four materials match a CPU copy of the formula at the centres of two faces, within 3/255 per channel.
- A smooth surface shows a highlight that a rough one does not. A metal tints its highlight. A dielectric's highlight is white.
- A normal map tilted towards +x, and one tilted up the image (−z on the top face), each match the formula. Each brightens towards its own light. A normal scale of 0 is byte-identical to no map.
- The metal-roughness texture multiplies the factors.
- Parts without a material draw within 1/255 of the lit shader. Materials off, or disabled again, are byte-identical.
- The material shader receives shadows: shadowed ground keeps only the ambient.
- Bad factors, an sRGB normal map, a foreign texture, a bad part material and a skinned renderer are refused.
