# ADR 0105 — Distant chunk detail

Status: Accepted
Related task/PR: `06.08` (Claude)
Supersedes / superseded by: none

## Context
`06.08` asks for distant world chunks drawn with a coarser representation, camera-relative.
GPT's partition (`06.01`, ADR 0089) gives regions of `region_size` metres with double-precision
bounds, and its chunk streamer (`06.04`, ADR 0093) holds a chunk's coarse and fine payloads.
Terrain meshes (`07.05a`, ADR 0095) already take a `step` for a coarser mesh over the same
heights. Nothing yet picks which detail a chunk is drawn at.

## Options
- Per-vertex geomorphing between levels: smooth, but needs a shader change and a second height
  stream. Too large for the first slice.
- Discrete levels chosen per chunk from camera distance, with hysteresis. Small, pure, testable
  without a GPU.
- Screen-space error metric: better choice of level, but needs the projection and a height
  error per level that the payloads do not carry yet.

## Decision
Discrete near/far detail per chunk, in `lykoware/distant_detail.hpp`:
- `chunk_distance` measures x/z metres from the eye to the region's nearest edge in double.
- `choose_chunk_detail` goes far at `far_from_metres` and back to near only inside
  `far_from_metres - hysteresis_metres`, so a camera on the line does not flip it every frame.
- `chunk_terrain_mesh` builds the near mesh at step 1 and the far mesh at `far_step` (above 1).
- `chunk_origin` is the region's corner as a `DVec3`; the renderer makes it relative to the eye
  before it becomes float (ADR 0021), as for every other world position.

## Contracts and compatibility
- Pure functions, safe on any thread. No renderer, GPU, ECS or streamer state is read or kept.
- The caller owns the mesh and its `Renderer::add_mesh` / `remove_mesh` lifetime. Swapping a
  chunk's detail is a remove and an add; the renderer API does not change.
- Bad bands (`far_from_metres` not finite or not above 0, hysteresis negative or not below
  `far_from_metres`, `far_step` below 2) and a negative or non-finite distance are refused with
  a reason and leave the output untouched. A `far_step` that does not divide the grid fails as
  `terrain_mesh` does.
- No existing hash, format or API changes.

## Consequences
- A far chunk costs about `1 / far_step²` of the near mesh's triangles.
- Every far vertex lies on a near vertex, but a near edge has samples between them, so a near
  chunk beside a far one can show a crack where the ground bends. Skirts or edge stitching are
  deferred.
- Only terrain has a coarse form. Buildings and props at distance (impostors) are deferred.
- No streaming payload is decoded here; turning a chunk's coarse bytes into heights is the
  caller's job until a payload format for it exists.

## Verification
`render.distant_detail` (CPU, no GPU): near and far meshes from one patch, the far one a
sixteenth of the triangles and every vertex on a near vertex; hysteresis and boundary jitter
flipping once; distances exact at the origin and 16 km out with a sub-metre camera-relative
offset; every refusal. The visual look of the transition and its frame cost at world scale are
not measured here: **Joshua runs this** with `06.11` on the RTX 4070 PC.
