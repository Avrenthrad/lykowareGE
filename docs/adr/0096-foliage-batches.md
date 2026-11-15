# ADR 0096 — Foliage scattered over terrain and drawn as a batch

Status: Proposed.
Related task/PR: `07.05b`, the second slice of `07.05`, branch `claude/07.05b-foliage`.
Supersedes / superseded by: none. Builds on ADR 0095 (terrain mesh), ADR 0088 (culling, batching, freeing meshes) and ADR 0035 (uploaded meshes).

## Context

- **What exists:** the renderer draws one entity per mesh. Batching (ADR 0088) groups draws that share state but still issues one draw per entity. A patch of ground with hundreds of plants would be hundreds of entities and draws.
- **Constraints:**
  - Plants must stand on the ground that is drawn, and the same patch must grow the same plants on every machine and every run, or replays and captures differ.
  - GPU instancing is not in the RHI yet (ADR 0088 leaves it for later).
  - Plants are drawing only. They must not need a physics body or the streamer.

## Options

- **One entity per plant.** Nothing new, but the ECS, the draw list and the push constants all grow with the plant count.
- **GPU instancing.** One mesh and a per-instance buffer. The right long-term path for plants that move or change, but it needs a new vertex input rate, shader variants and shadow support in the RHI first.
- **Bake the plants of a patch into one mesh on the CPU.** One draw per patch with today's renderer; culled and freed with the patch. Costs vertex memory per plant and cannot move a single plant afterwards.

## Decision

- **A baked batch.** `render::foliage_batch(plant, instances, batch, error)` in `lykoware/foliage.hpp` copies the plant mesh once per `FoliageInstance { position, yaw, scale }`: scaled, turned about +Y, then moved. Normals turn with it; texture coordinates are copied. The result is a `MeshData`, drawn as one entity and one draw.
- **A seeded scatter.** `render::scatter_foliage(FoliageScatter, instances, error)` places plants over a `TerrainPatch`:
  - Each cell gets `density × spacing²` plants: the whole part always, one more with the fractional part's chance.
  - Each cell has its own generator, seeded from the scatter's seed and the cell's index (splitmix64). A cell's plants therefore do not depend on any other cell's heights.
  - A plant's height is taken on the cell's triangle, split as `terrain_mesh` splits it, so it stands exactly on the drawn surface at step 1.
  - A plant on a triangle steeper than `max_slope` is dropped. It still uses its random numbers, so the others do not shift.
  - Placement uses only `+ − × ÷` and `sqrt` on floats, which IEEE 754 fixes, so positions are identical across compilers and platforms. Yaw is applied in the batch with `sin` and `cos`; that may differ in the last bit between C libraries, which changes no placement and no count.
- **Who owns what.** Both functions are pure and hold nothing. The renderer owns the uploaded batch like any mesh and frees it with `remove_mesh` (ADR 0088). The caller keeps or drops the instance list.
- **Limits.** At most `foliage_max_instances` (1,048,576) plants per scatter or batch, and the batch must fit `mesh_max_vertices` and `mesh_max_indices`.
- **Failure.** Each function returns false with a reason and leaves its output untouched; the header lists the cases. A missing shader is not a new case: a batch draws with the lit shaders the renderer already requires.
- **Not in this slice.** Leaf textures cut out by alpha, two-sided cards, wind, fading plants in and out by distance, and GPU instancing. The lit shader has no alpha test, so plants are solid meshes for now.

## Contracts and compatibility

- New: `lykoware/foliage.hpp` with `render::FoliageInstance`, `FoliageScatter`, `foliage_max_instances`, `scatter_foliage`, `foliage_batch`.
- Nothing existing changes. No scene format stores foliage yet; that is GPT's format.
- Threading: pure functions, callable from any thread. Upload is on the Device's thread, as `add_mesh` today.
- Changing the scatter's formula changes where plants stand for a given seed. Treat that as a visible change and say so in the PR that does it.

## Consequences

- A patch's plants cost one draw and one bounding sphere, and go away with one `remove_mesh`.
- A batch's memory grows with plants times plant vertices. Patches should stay small enough to cull usefully; the streamer (`06.04`) will decide patch size.
- Shadows, materials and culling apply to a batch as to any mesh.

## Verification

- `render.foliage_mesh` (no GPU): the same seed gives the same plants and another seed others; density 2 over 64 m² gives exactly 128 and density 2.5 about 160; every plant of a bumpy field stands within 1 mm of the `terrain_mesh` surface; raising one corner sample moves only that cell's plants; with `max_slope` 0.7 no plant stands on a 63° slope, and with 1.5 the slope is planted, so the fixture can tell. A quarter turn, scale and move of a known triangle land where stated; a three-plant batch is a valid mesh with offset indices. Each refusal leaves the output untouched.
- `render.foliage` (lavapipe locally; CI on the five hosted jobs): 192 plants are one entity and one draw. The image matches the same plants drawn as 192 entities within 2% of the covered pixels. With the batch's draw removed the image is empty.
