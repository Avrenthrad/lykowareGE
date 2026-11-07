# ADR 0035 — Our own glTF reader; drawing loaded meshes

Status: Proposed.
Related task/PR: Model loading, PR 1 (branch `claude/gltf-meshes`), stacked on the audio PRs (`claude/audio-spatial`) to avoid conflicts in the sandbox and status files. It does not depend on audio code.
Supersedes / superseded by: none.
- Implements the mesh part of [ADR 0004](0004-data-formats.md) (glTF 2.0 for meshes).
- Extends the renderer from [ADR 0026](0026-renderer.md).
- Uses nlohmann/json, vendored in [ADR 0031](0031-scenes-and-command-replay.md).

## Context

ADR 0004 chose glTF 2.0 for meshes, and "basic assets" is a Phase 1 deliverable. The renderer draws only a built-in cube.

On 2026-11-05 the owner chose to write Lykoware's own reader rather than vendor cgltf, and to start with shapes only: meshes, no textures.

Model files will come from outside the engine (exporters, the owner's own models, later mods), so the reader must treat every file as untrusted.

## Options

- **Reader:**
  - **our own reader** on the vendored nlohmann/json;
  - **cgltf** (MIT, single file, covers the whole spec);
  - **tinygltf** (MIT; it also bundles image decoding).

  The owner chose our own. It reads only what Lykoware needs and refuses the rest with a reason. It follows the project's other readers (WAV, scenes) and their tests.
- **Output shape:**
  - **one merged mesh per model**, with each node's transform applied;
  - **a node hierarchy with separate meshes.**

  One merged mesh was chosen for now: one draw per entity, as the renderer works today. A hierarchy comes when models need moving parts (animation, ADR to come).
- **First scope:**
  - **shapes only:** positions, normals and triangles;
  - **shapes and textures.** That needs texture support in the RHI and an image decoder first.

  The owner chose shapes only.

## Decision

Proposed:

- **`lykoware/mesh.hpp`:**
  - **`MeshData`:** positions, unit normals and triangle indices, in Lykoware's conventions (metres, right-handed, +Y up, counter-clockwise front faces, which glTF shares).
  - **`valid_mesh()`** checks everything the renderer relies on.
  - **`bounds()`.**
  - **Limits:** 8,388,608 vertices and 50,331,648 indices, so each fits one RHI buffer.
- **`lykoware/gltf.hpp`** (nlohmann/json stays private to the engine):
  - **`gltf::read(bytes, external, error)`:**
    - reads .gltf JSON and .glb binary;
    - buffers may be inside the .glb, in base64 data URIs, or in other files read through a caller's `BufferReader`;
    - float VEC3 positions and normals; 8-, 16- and 32-bit indices; byte strides;
    - triangle lists, strips and fans; points and lines are skipped;
    - node hierarchies with matrices or translation, rotation and scale.
  - **`gltf::load(path, error)`:**
    - reads a file of up to 1 GiB;
    - reads its external buffers only from the model's folder or below it. Absolute paths, `..` and URI schemes are refused, so a model cannot make the engine read other files.
  - **What comes out:** the default scene (`scene`, else the first), depth first in document order, merged into one `MeshData`:
    - positions go through each node's world transform;
    - normals go through its inverse transpose, made unit length;
    - a mirroring transform (negative determinant) reverses triangle order so faces still point outwards;
    - primitives without normals get flat normals, as the spec requires;
    - a flattened transform (zero determinant) adds nothing.
  - **Refused, with a reason naming the item (e.g. "accessor 3 runs past the end of its buffer view"):**
    - malformed JSON or .glb containers;
    - glTF versions other than 2.x;
    - any `extensionsRequired` (Draco and meshopt compression among them);
    - sparse accessors;
    - data outside its buffer or view;
    - indices past the last vertex;
    - non-finite values;
    - node graphs that are not trees (cycles, two parents);
    - a node with both a matrix and translation, rotation or scale;
    - no scene, or no triangles.
  - **Bounds before allocation:** sizes are checked before anything is allocated for them, so a small file cannot make the reader reserve memory for data it does not have.
- **Renderer:**
  - **`Renderer::add_mesh(MeshData)`** uploads a mesh and returns a `MeshId`. The mesh stays until the renderer is destroyed.
  - **`MeshRenderer::mesh`** (a new last field, so existing code is unchanged) draws that mesh instead of the built-in shape. An id the renderer did not make is not drawn.
  - **Normals:** model normals now go through rotation × (1 / scale), the inverse transpose, instead of rotation alone. Uneven scale lights slanted faces correctly. Built-in cubes look the same, because their normals are axis-aligned.
- **Sandbox:**
  - **`--window --vulkan --model FILE`** loads a .gltf or .glb before any window or GPU work, and fails clearly for a bad file;
  - the model stands to the left of the crates, 3 m at its largest side, turning slowly.

## Contracts and compatibility

- New public headers `lykoware/mesh.hpp` and `lykoware/gltf.hpp`; new sources `engine/src/mesh.cpp` and `engine/src/gltf.cpp`.
- **Renderer:** gains `add_mesh` and `MeshId`, and `MeshRenderer` gains a last field `mesh`. Existing aggregate initialisers are unchanged.
- **Threading:** `gltf::read` and `gltf::load` share no state, so they can run on any thread, for example a loading thread. `add_mesh` runs on the device's thread, like the rest of the renderer.
- **Lifetime:** a `MeshId` is valid only with the renderer that made it. There is no removal yet, because freeing a mesh a frame in flight may still use needs frame tracking.
- **Ownership:** a `MeshId` carries its renderer's process-local owner token, as ECS and physics handles do (ADR 0030). Another renderer's id is not drawn, even one with the same position, such as an id kept from a renderer that was destroyed and replaced. Codex's health check (#47) found such ids accepted before this rule existed.
- **Not yet:**
  - materials and colours from the file (a `MeshRenderer` colour applies to the whole model);
  - textures and texture coordinates;
  - skins, morph targets and animation;
  - cameras and lights from the file;
  - sparse accessors;
  - KHR extensions;
  - quantized attributes;
  - streaming;
  - an asset cache or IDs in scenes.

## Consequences

- Real models can be shown, from any exporter that writes plain glTF 2.0 (Blender's default .glb included). Compressed or extension-dependent files are refused with the extension's name.
- One more parser of untrusted input lives in the engine. Its tests, the bounds-before-allocation rule and a fuzz run are the guard; a continuous fuzz job would add more.

## Verification

- **New test `assets.gltf`.** Every model is built in the test, in memory or under the build directory; no model files are committed.
  - **Loads:**
    - a triangle without indices or normals gets the face normal;
    - an indexed quad as .gltf and as .glb gives identical meshes;
    - bounds;
    - 8- and 32-bit indices;
    - interleaved data with a byte stride.
  - **Node transforms:**
    - translation;
    - a parent scale combined with a child translation;
    - matrices equal to translation, rotation and scale;
    - a quarter turn;
    - uneven scale bends a slanted normal by the inverse transpose;
    - a mirror keeps triangles facing outwards;
    - `scene` selects the scene;
    - several meshes merge with offset indices.
  - **Primitive modes:**
    - strips and fans unroll in the spec's order, facing their given normals;
    - lines beside triangles are skipped;
    - a model of lines only has no triangles.
  - **Refusals:** not JSON; version 1.0; a required extension (named in the error); no scene; a node that is its own child; two parents; a matrix with a scale; a missing mesh; an accessor past its view; a view past its buffer; a sparse accessor; 16-bit positions; an index out of range; a NaN position; mismatched normals; .glb version 1; a truncated .glb; bad base64; an external buffer with no reader.
  - **Files:**
    - a percent-encoded external URI;
    - `load()` of .gltf with its .bin and of .glb;
    - `../secret.bin`, absolute paths, `file://`, and `sub/../../` are refused;
    - a missing file is refused.
  - **`valid_mesh`.**
- **New test `render.model`** (lavapipe; skipped without Vulkan):
  - a glTF ramp read by `gltf::read`, uploaded and drawn with a 3× vertical stretch;
  - its brightness matches the lighting formula with the inverse-transpose normal: 167 measured against 167 expected; the old rotation-only normal gives 221;
  - a built-in cube draws beside it;
  - an unknown mesh id is not drawn, and the sky shows there;
  - a mesh id from a destroyed renderer is not drawn by its replacement, though both have the same position;
  - an empty mesh is refused;
  - the same image 16 km out;
  - no validation errors.
- **New test `sandbox.model`:** the sandbox shows the .glb that `assets.gltf` writes (a CTest fixture), headless on lavapipe.
- **Deliberate breaks:** 19, each caught:
  - missing flat normals;
  - strips not alternating;
  - fans around the wrong vertex;
  - mirrors keeping winding;
  - normals through the plain matrix;
  - accessor or view bounds unchecked;
  - cycles not detected (the test hangs);
  - required extensions ignored;
  - sparse accessors read;
  - folder escape allowed;
  - URIs not percent-decoded;
  - .glb version unchecked;
  - byte stride ignored;
  - 16-bit positions read as float;
  - node transforms dropped;
  - renderer normals ignoring scale;
  - unknown meshes drawn as cubes;
  - added meshes ignored.
- **A weakness found and fixed:** the first strip and fan checks used flat normals, which always agree with the winding. They now give normals, so a wrong order fails.
- **Fuzzing:** a local fuzz run (not committed) fed 200,000 mutated .gltf and .glb files to `gltf::read` under ASan/UBSan. 3,508 still loaded, the rest were refused, and nothing crashed.
- **clang-tidy** flagged a division that could only be by zero with a zero element size. An explicit guard was added.
- **Local results:** 47/47 pass in the Debug, ASan/UBSan and clang-tidy presets. A GCC `-Wshadow -Wconversion` sweep of the new and changed files is clean.
- **CI:** blocked for the whole account since 2026-11-05 (see ADR 0033); to be recorded.
- **Owner check wanted:** export any model from Blender as .glb and run `lykoware_sandbox --window --vulkan --model your.glb`.
