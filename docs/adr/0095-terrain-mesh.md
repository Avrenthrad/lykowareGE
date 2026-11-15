# ADR 0095 — Terrain drawn from a grid of heights

Status: Proposed.
Related task/PR: `07.05a`, the first slice of `07.05`, branch `claude/07.05a-terrain`.
Supersedes / superseded by: none. Builds on ADR 0035 (uploaded meshes), ADR 0082 (the stated light), ADR 0088 (culling and freeing meshes) and ADR 0021 (camera-relative drawing).

## Context

- **What exists:** physics collides with terrain (`physics::HeightFieldShape`). Nothing draws it; samples draw a flat box instead.
- **Constraints:**
  - The drawn ground must be the ground a body stands on, cell by cell. A height field cell is two triangles, so the draw must split each cell the way the collision does.
  - Drawing must not need a physics body, and collision must not need a renderer. Headless servers, tools and replays use one without the other.
  - World streaming (`06.04`) and distant drawing (`06.08`) are later work and GPT's chunk format does not exist yet. This slice must not guess it.

## Options

- **A terrain type inside the renderer** (its own vertex format, shader and draw path). Room for clipmaps and splat shading later, but a second path through shadows, materials, culling and batching now.
- **A CPU function that turns heights into a `MeshData`.** The renderer draws it like any mesh, so lighting, shadows, materials, culling, batching and freeing already work. A different representation can replace it later behind the same inputs.
- **Draw from the physics body.** One source of heights, but it ties drawing to a live physics world and to Jolt's 1 cm height compression.

## Decision

- **A CPU mesh builder.** `render::terrain_mesh(TerrainPatch, MeshData&, error)` in `lykoware/terrain_mesh.hpp`. `TerrainPatch` holds a span of heights with the layout of `physics::HeightFieldShape`: `samples_per_side` squared, row-major, sample (x, z) at `{x * spacing, height, z * spacing}` in the mesh's own space. The caller places it with the entity's `Transform`, as it places the body with `BodyDesc::position`.
- **The same split as collision.** Each cell from (x, z) to (x + 1, z + 1) is two triangles along that diagonal, facing up. This is the split Jolt's height field uses; the test measures it instead of trusting it.
- **Normals and texture coordinates.** A normal comes from the height difference between the neighbouring used samples on each axis, one-sided at an edge. Patches that share an edge therefore do not share edge normals yet; that waits for the chunk layout. Texture coordinates are x and z divided by `texture_metres`, so a repeating texture tiles at a stated size.
- **A coarser patch over the same ground.** `step` uses every step-th sample; `samples_per_side - 1` must be a multiple of it. The coarse mesh's vertices are samples of the fine field, so its corners and outline match.
- **Draw and collision stay separate.** The function reads heights and returns a mesh. It does not include or call physics. The renderer owns the uploaded buffers and frees them with `remove_mesh` or with itself (ADR 0088); the physics world owns the body. Either can exist without the other.
- **Streaming is not here.** A patch is whatever grid the caller hands over. Which chunks exist, when they load and how neighbours join belongs to `06.04`; choosing a coarse patch by distance belongs to `06.08`.
- **Failure.** `terrain_mesh` returns false with a reason and leaves the output untouched for: fewer than 2 samples per side, a wrong height count, a step of 0 or one that does not divide, a spacing or `texture_metres` not finite or not above 0, a height not finite, or a result past `mesh_max_vertices` or `mesh_max_indices`. A missing shader is not a new case: terrain uses the lit shaders the renderer already requires.

## Contracts and compatibility

- New: `lykoware/terrain_mesh.hpp` with `render::TerrainPatch` and `render::terrain_mesh`.
- Nothing existing changes. No scene format stores terrain yet; that is GPT's format.
- Threading: a pure function of its inputs, callable from any thread. Uploading the result is on the Device's thread, as `add_mesh` today.

## Consequences

- Terrain can be drawn now with lighting, shadows, materials, culling and batching, and freed when its patch goes away.
- A patch is one mesh with one bounding sphere, so culling is per patch. A 4096-sample field needs a step or smaller patches to fit the mesh limits.
- Blending several ground textures by slope or height, seams between patches of different step, and geometry that changes detail smoothly are later slices.
- `07.05` is split: `07.05a` terrain (this ADR), `07.05b` foliage, `07.05c` water, `07.05d` sky and distance fog.

## Verification

- `render.terrain_mesh` (no GPU): layout and texture coordinates of a 4 x 4 patch, upward triangles, exact normals on a plane, the step-4 mesh of a 9 x 9 field, and each refusal. A bumpy 8 x 8 field placed away from the origin as a static `HeightFieldShape` is hit by 784 downward rays; each lands within 3 cm of the mesh. The same rays miss a field split along the other diagonal by more than 0.2 m, so the fixture can tell.
- `render.terrain` (lavapipe locally; CI on the five hosted jobs): flat ground and a 0.5 m/m ramp match the lit formula within 2/255. With the terrain's draw removed the image is empty. A cube under the ground is hidden, and shows once the ground is gone. The step-4 patch covers the same pixels within 1%.
