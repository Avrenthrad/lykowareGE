# ADR 0051: CPU skinning reference and rendered pose checks

Status: Proposed; implementation on `codex/skinned-rendering`, stacked on Claude's glTF skins source `15383a27bcc788191bb6646f5d01f27a4673ea47` (ADR 0050).

## Context

The animation core samples clips and makes skinning matrices. The glTF reader now supplies per-vertex joints and weights. A trusted deformation result is needed before GPU skinning and an animated sandbox character can be checked. Building a per-frame mesh-upload loop into the renderer now would introduce allocation and GPU-lifetime costs without a measured budget.

## Decision

Add `animation::deform_mesh` in `lykoware/skinning.hpp` as an owned, pure CPU reference. It takes a valid bind mesh, four joint slots and weights per vertex, and an affine matrix palette of at most 256 entries. It returns a separate MeshData usable by the existing renderer, tooling or a future GPU comparison test.

Use linear-blend skinning: normalise near-unit weights and blend the matrices, then transform each position. Normals use the inverse transpose of the blended linear matrix, followed by normalisation. This is the affine blend's normal transform, not a blend of per-joint inverse-transpose matrices. Cofactors and positions use double intermediates to avoid overflow for finite float inputs; the determinant's sign is retained for reflections. UVs and triangle indices are preserved; winding is not repaired and normals are not recomputed from neighbouring deformed triangles. Weight gradients and smooth-surface reconstruction are outside this reference's model.

Refuse invalid bind meshes, missing skin entries, non-finite/negative/non-unit weights, weighted joint indices outside the palette, empty/oversized/non-finite/projective palettes, singular blends and unrepresentable results. Zero-weight slots are ignored. Build the result separately and replace the output only on success, allowing input/output aliasing. Allocation exceptions propagate with output unchanged.

This is an offline/capture correctness slice. It adds no mutable GPU buffers, shader format, descriptor layout, frame-loop upload or simulation state. Immutable posed meshes can be uploaded once through the existing renderer for captures. No per-frame performance or production GPU-skinning claim follows from this work. Memory/time scale with vertices and the copied mesh; do not use it as the flagship's crowd-animation path.

## Verification

- `animation.deform`: hand-calculated joint blends; identity; source/output aliasing; topology/UV preservation; rotation/non-uniform scale; negative determinant; inverse transpose of the blend; near-unit weight normalisation; invalid inputs and late failures leave output intact.
- `render.skinning`: generate a two-joint glTF model and translation clip in memory, load it through the real reader, sample the clip, deform the mesh, upload through the existing renderer, and capture real Vulkan pixels. Compare with an independently translated mesh; check departure from the bind location, arrival with red/blue UV texture intact, and independent poses of the same model in one scene. Save captures under ignored build output.
- Unavailable Vulkan is an explicit skip; actual execution and any remaining platform limits are recorded in STATUS and the PR. Existing static-scene goldens must continue to pass.

## Next

Use this as a reference when implementing a measured GPU palette/vertex path and playing clips on the sandbox character. CPU pose snapshots are not a substitute for those steps. Foot IK, animation/physics blending and ragdoll remain later tasks. Windows and target-hardware gates are unchanged.
