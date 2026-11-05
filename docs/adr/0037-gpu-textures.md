# ADR 0037 — Textures in the RHI and the renderer

Status: Proposed.
Related task/PR: Textures, PR 2 of 3 (branch `claude/gpu-textures`), stacked on PR 1 ([ADR 0036](0036-image-decoding.md)). PR 3 reads glTF materials and texture coordinates.
Supersedes / superseded by: none. Extends [ADR 0025](0025-rhi-meshes.md) (meshes in the RHI) and [ADR 0026](0026-renderer.md) / [ADR 0035](0035-gltf-meshes.md) (the renderer).

## Context

Decoded images (ADR 0036) need to reach the GPU and be sampled by the lit shader. The RHI has vertex and index buffers and push constants, but no images to sample and no descriptor sets.

## Options

- **Binding textures:**
  - **one descriptor set per texture**, with a layout every texture shares, bound per draw;
  - **bindless** (one big array of textures indexed in the shader). It scales better, but needs descriptor indexing, more validation, and a different shader model.

  One set per texture was chosen. It is simple, works on every Vulkan 1.3 device, and can move to bindless later behind `MeshDraw::texture`.
- **Mipmaps:**
  - **made on the CPU** at load time by averaging 2×2 texels, in linear light for sRGB colour;
  - **made on the GPU** with image blits. Blit filtering differs between GPUs, needs the format to support linear blits, and averages sRGB values in whatever space the driver chooses.

  The CPU was chosen: the same result on every GPU, gamma-correct, and testable. Load-time cost is small next to decoding.
- **Untextured meshes:**
  - **a 1×1 white texture**, so there is one pipeline;
  - **a second, untextured pipeline.**

  The white texture was chosen. Existing scenes look exactly as before.

## Decision

Proposed:

- **`rhi::Texture`** (`lykoware/rhi/texture.hpp`): a 2D RGBA8 texture with its sampler and descriptor set.
  - **Formats:** `Rgba8Srgb` for colour (shaders read linear values) and `Rgba8Unorm` for data.
  - **Mipmaps:** an optional full chain, made on the CPU. sRGB colour channels are averaged in linear light; alpha and Unorm channels as stored.
  - **Sampling:** wrap (Repeat, MirroredRepeat or ClampToEdge) and filter (Linear or Nearest, including between mip levels).
  - **Upload:** through a staging buffer and the Device's one-shot submit. The image then stays in `SHADER_READ_ONLY_OPTIMAL`.
  - **Refused (InvalidArgument):** a zero size, a side over the device's `maxImageDimension2D`, data over 256 MiB, or data that is not width × height × 4 bytes.
- **Device:** owns one descriptor set layout, a combined image sampler at binding 0 for the fragment stage. Every texture's set and every texturing pipeline use it, so any texture works with any such pipeline.
- **Pipelines:**
  - `VertexLayout::PositionNormalUv`: 32-byte vertices, with a float2 texture coordinate at location 2;
  - `GraphicsPipelineDesc::texture`: the pipeline reads the texture at set 0, binding 0.
- **Mesh draws:** `MeshDraw::texture` is required when the pipeline reads a texture and refused when it does not. A draw binds its texture's set, skipping the bind when consecutive draws use the same texture.
- **The engine's lit shader** (`engine/shaders/lit.slang`) now takes texture coordinates and multiplies the base colour by the texture.
  - `tests/shaders/mesh.slang` keeps the old untextured shader, so `rhi.mesh` still tests the `PositionNormal` path.
  - `tests/shaders/textured.slang` is an unlit textured shader for `rhi.texture`.
- **Renderer:**
  - **Pipeline:** it uses `PositionNormalUv` and textures.
  - **Cube:** the built-in cube has texture coordinates, so each face shows the whole texture, upright on the side faces.
  - **`MeshData::uvs`** holds texture coordinates: none, or one per position, with (0, 0) the top-left as in glTF.
  - **`Renderer::add_texture(RgbaImage, TextureSettings)`** returns a `TextureId`, held until the renderer is destroyed.
    - Like a `MeshId`, a `TextureId` carries its renderer's process-local owner token (ADR 0030), so another renderer's id is not drawn even when its position matches.
    - Codex's health check (#47) found an id kept from a destroyed renderer drawing its replacement's texture before this rule existed.
  - **`MeshRenderer::texture`** (a new last field) multiplies `colour`. The default uses the 1×1 white texture. An id the renderer did not make is not drawn, as for meshes.

## Contracts and compatibility

- New public header `lykoware/rhi/texture.hpp`; new source `engine/src/rhi/texture.cpp`.
- **New fields:** `GraphicsPipelineDesc::texture`, `MeshDraw::texture`, `MeshData::uvs`, `MeshRenderer::texture`. All have defaults, so existing initialisers are unchanged. `VertexLayout::PositionNormalUv` and `GraphicsPipeline::texture()` are new.
- **Lit shader inputs changed:** callers of the renderer (sandbox, `lyko`, tests) are unchanged, because they compile `lit.slang` and pass it to the renderer. Custom pipelines built from `lit.slang` must now use `PositionNormalUv` and a texture.
- **Lifetime:** a texture must outlive any frame in flight that samples it. Renderer textures live as long as the renderer, which is destroyed after the swapchain (whose destructor waits for the GPU).
- **Not yet:**
  - anisotropic filtering (it needs a device feature);
  - texture compression (BC/ASTC/KTX2);
  - alpha blending and alpha masking;
  - normal, metal-roughness and emissive maps;
  - per-axis wrap modes;
  - a sampler cache (one sampler per texture for now);
  - removing textures while frames are in flight;
  - streaming.

## Consequences

- Models can show colour textures, sampled correctly in linear light with mipmaps. Texture quality at a distance is the same on every GPU, because the mipmaps are not driver-made.
- One descriptor set per texture and one bind per draw is fine for hundreds of textures. Thousands per frame would want bindless; measurements decide.

## Verification

- **New test `rhi.texture`** (lavapipe; skipped without Vulkan). Unlit quads into small targets, so pixels are exact:
  - 4×4 distinct texels map one-to-one onto 4×4 pixels, rows from the top;
  - an sRGB texture is read as linear light (sRGB 64 → 13, 128 → 55);
  - a 64×64 black-and-white checkerboard seen at 1/16 size picks mip level 4 and gives 128, half brightness in linear light. Averaging sRGB values would give 55; with mipmaps off, the same view aliases to pure black or white;
  - 64×64 has 7 levels and 5×3 has 3;
  - Repeat, ClampToEdge and MirroredRepeat give `rbrb`, `rbbb` and `rbbr` over two repeats;
  - misuse is refused: zero size, wrong data size, wider than the device, a texturing pipeline without a texture, and a texture for an untextured pipeline;
  - no validation errors.
- **New test `render.texture`:**
  - a quad with a 2×1 red|blue texture shows red on its left half and blue on its right, each lit to the formula's value (115 = (0.25 + 0.75 × n·l) × 255);
  - an untextured cube keeps its colour;
  - an unknown texture id is not drawn;
  - a texture id from a destroyed renderer is not drawn by its replacement, though both have the same position, while the replacement's own id is;
  - an empty image is refused.
- **Unchanged pictures:** `render.scene`, `render.model` (the stretched ramp still measures 167), `scene.cli_capture`, `rhi.mesh` (on its kept untextured shader) and both sandbox tests pass as before.
- **Deliberate breaks:** 12, each caught:
  - mipmaps averaged as sRGB values (55 instead of 128);
  - sRGB stored as Unorm;
  - wrap ignored;
  - filter ignored;
  - only level 0 uploaded;
  - mipmaps taken from one texel;
  - the descriptor set not bound;
  - a missing texture allowed;
  - texture coordinates read from the normal;
  - an unknown texture drawn white;
  - the texture id ignored;
  - texture coordinates not uploaded.
- **Local results:** 50/50 pass in the Debug, ASan/UBSan and clang-tidy presets, with no validation errors. A GCC `-Wshadow -Wconversion` sweep of the changed files is clean.
- **CI:** blocked for the whole account since 2026-11-05 (see ADR 0033); to be recorded.
