# ADR 0038 — glTF materials, texture coordinates and mesh parts

Status: Proposed.
Related task/PR: Textures, PR 3 of 3 (branch `claude/gltf-materials`), stacked on PR 2 ([ADR 0037](0037-gpu-textures.md)) and PR 1 ([ADR 0036](0036-image-decoding.md)).
Supersedes / superseded by: none. Extends the glTF reader of [ADR 0035](0035-gltf-meshes.md).

## Context

The renderer can draw textured meshes (ADR 0037), but the glTF reader returns only a shape: one merged mesh with no texture coordinates and no materials. A model's primitives can each have their own material, so a single merged mesh with one colour cannot show it.

## Options

- **Several materials in one model:**
  - **parts:** one vertex and index buffer per model, with each primitive a range of indices and its own material;
  - **one mesh per primitive.**

  Parts were chosen: one upload, one entity, and draws differ only in their index range, colour and texture. The RHI gains `MeshDraw::first_index` for this.
- **Images:**
  - **decoded inside the reader**, which returns `RgbaImage`s;
  - **handed back as encoded bytes.**

  Decoded was chosen: the reader already confines file access to the model's folder (ADR 0035) and limits sizes, and callers then cannot skip those checks.
- **API:**
  - **new `read_model` and `load_model`** returning a `gltf::Model`;
  - **change `read` and `load`.**

  New functions were chosen. `read` and `load` still return the shape alone and decode no images, for callers that need only geometry (physics, bounds).

## Decision

Proposed:

- **`lykoware/gltf.hpp`:**
  - **`read_model` and `load_model`** return a `gltf::Model`:
    - `mesh`;
    - `parts`: one per primitive, in scene order, each an index range with an optional material;
    - `materials`: a base colour factor and an optional base colour texture;
    - `textures`: an image and its sampling;
    - `images`, decoded to RGBA.

    Only what the parts use is listed, each once, in first-use order.
  - **Texture coordinates** come from `TEXCOORD_0`: float, or normalised unsigned 8- or 16-bit, as glTF allows. Vertices of primitives without them read (0, 0). Flat-normal primitives keep each corner's coordinate, including mirrored ones.
  - **Materials:** `pbrMetallicRoughness.baseColorFactor` (each value 0..1) and `baseColorTexture`. A base colour texture on a coordinate set other than 0 is refused for now.
  - **Samplers:** `wrapS` (Repeat, MirroredRepeat, ClampToEdge) is used for both axes. `magFilter` NEAREST selects nearest filtering. `minFilter` decides whether mipmaps are used.
  - **Images:**
    - an image can be in a buffer view (with `mimeType` `image/png` or `image/jpeg`), a data URI, or a file beside the model, confined to the model's folder like buffers;
    - decoded through `decode_image` (ADR 0036);
    - at most 268,435,456 pixels in all per model.
  - **Refused, naming the item:** unknown sampler values, a texture without a source, an image with neither a URI nor a buffer view, and an image that does not decode.
  - **`read` and `load`** return the shape (now with texture coordinates) and do not touch materials or images.
- **Renderer:**
  - **`MeshPart`:** an index range, a colour that multiplies `MeshRenderer::colour`, and a texture that takes the place of `MeshRenderer::texture`.
  - **`Renderer::add_mesh(mesh, parts)`:** each part is a draw. It refuses a part outside the indices, one that is not whole triangles, or one with a texture id this renderer did not make, including another renderer's id at the same position (ADR 0030; found by Codex's health check, #47).
  - **`add_mesh(mesh)`** is one whole-mesh part.
  - **`last_draw_count()`** counts entities, so a mesh of several parts counts once.
- **RHI:** `MeshDraw::first_index` sets where a draw's indices start, and is checked against the index buffer.
- **`math::Vec2`** gains `operator==`, like `Vec3`.
- **Sandbox:** `--model` uses `load_model`. It uploads each texture with its sampling, and the mesh as parts with the materials' colours and textures. The model's own colours show, because its `MeshRenderer` colour is white.

## Contracts and compatibility

- `read`/`load` and their results are unchanged, except that `MeshData::uvs` may now be filled.
- New: `gltf::Model`, `Part`, `Material`, `Texture`, `Sampling`, `Wrap`, `read_model`, `load_model`, `render::MeshPart`, the `add_mesh(mesh, parts)` overload, and `rhi::MeshDraw::first_index` (last field, default 0).
- `Renderer::last_draw_count()` now counts entities, not draws. The two were the same before parts existed.
- `tests/support/png_writer.hpp` is a header-only PNG and base64 writer for tests.
- **Not yet:**
  - metal-roughness, normal, occlusion and emissive maps;
  - alpha modes (blend and mask);
  - double-sided materials (back faces are culled);
  - `KHR_texture_transform` and other material extensions;
  - texture coordinate sets other than 0;
  - separate wrap modes per axis;
  - vertex colours;
  - instancing of meshes shared between nodes (each use is merged in again).

## Consequences

- A model exported from Blender with base colour textures shows its colours and textures in the sandbox.
- Images are decoded on whatever thread loads the model, ready for a loading thread later.

## Verification

- **`assets.gltf`**, new materials cases. Models are built in the test, with PNGs made by `tests/support/png_writer.hpp`:
  - **One textured quad:** the part, the material's factor and texture, the sampler's wrap, nearest and no-mipmap settings, the decoded image's exact pixels, and the texture coordinates are all read.
  - **The same model as .glb:** reads identically.
  - **`read()` versus `read_model()`:** `read()` gives the same shape. It ignores a broken image, which `read_model()` refuses, naming it.
  - **Three primitives:**
    - parts follow the primitives;
    - a material used twice is listed once;
    - a primitive without texture coordinates reads (0, 0).
  - **Normalised coordinates:** 8- and 16-bit normalised coordinates are read, and unnormalised integers are refused.
  - **Flat normals:** they keep each corner's coordinate under a mirror.
  - **Refusals:**
    - a second coordinate set;
    - a factor above 1;
    - a missing texture;
    - a texture without a source;
    - an unknown wrap mode;
    - a non-PNG/JPEG mime type.
  - **Files:**
    - an image in a file beside the model (percent-encoded name) and in a data URI loads;
    - an image outside the folder is refused;
    - the test writes `textured.glb` for `sandbox.model`.
- **`render.model`**, new two-material case (GPU):
  - two primitives read by `read_model` and uploaded as the sandbox does;
  - the textured part shows its red|blue texture and the other its green factor, each lit to the formula's value (115);
  - two parts count as one entity;
  - a part outside the indices, one with an unknown texture, and one with another renderer's texture id at a real position, are refused;
  - the frame is written as `render-model/materials.ppm`.
- **`sandbox.model`:** now shows the textured .glb (1 material, 1 image).
- **Deliberate breaks:** 15, each caught:
  - TEXCOORD_0 ignored;
  - 8-bit coordinates scaled wrongly;
  - mirrored flat corners keeping old coordinates;
  - materials not recorded;
  - materials not shared;
  - sampler wrap ignored;
  - the mipmap setting inverted;
  - a second coordinate set allowed;
  - the factor range unchecked;
  - the image mime type unchecked;
  - earlier vertices not padded;
  - part colour ignored;
  - part texture ignored;
  - `first_index` ignored;
  - parts unchecked.
- **Fuzzing** (local): 150,000 mutated .gltf and .glb files, textured ones included, through `read_model` under ASan/UBSan, with no crash.
- **Local results:** 50/50 pass in the Debug, ASan/UBSan and clang-tidy presets. A GCC `-Wshadow -Wconversion` sweep of the changed files is clean.
- **CI:** blocked for the whole account since 2026-11-05 (see ADR 0033); to be recorded.
- **Owner check wanted:** export a textured model from Blender as .glb and run `lykoware_sandbox --window --vulkan --model your.glb`.
