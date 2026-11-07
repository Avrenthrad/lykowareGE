# ADR 0050 — Reading glTF skins and animations

Status: Proposed.
Related task/PR: Phase 2 PR 5 (branch `claude/gltf-skins`), stacked on the skeletal animation core ([ADR 0049](0049-skeletal-animation-core.md)).
Supersedes / superseded by: none. Extends our own glTF reader ([ADR 0035](0035-gltf-meshes.md), [ADR 0038](0038-gltf-materials.md)).

## Context

- **What exists:** ADR 0049 gave the engine skeletons, clips and skinning, but nothing fills them from a file.
- **The format:** glTF 2.0 is the engine's mesh format (ADR 0004). Its skins and animations map directly onto ADR 0049's types.
- **The gap:** the reader (ADR 0035) ignored skins and animations, and placed every mesh by its node's transform.

## Options

- **Where the skin data goes:**
  - **into `MeshData`,** as per-vertex joints and weights every mesh carries;
  - **into a separate `SkinnedModel`** next to the mesh.

  `SkinnedModel` was chosen:
  - `MeshData`, `valid_mesh` and the renderer stay as they are until skinned drawing (the next PR) decides how the GPU gets joints and weights;
  - `read` and `read_model` keep exactly their present behaviour.
- **What the skeleton holds:**
  - **only the skin's joints;**
  - **the joints and every node above them.**

  The second was chosen. Exporters put transforms above the root bone: Blender's armature node, for example, with its rotation and scale. The inverse bind matrices assume them, and a joint's parent may be a non-joint node. Keeping those nodes as joints, with identity inverse bind matrices, poses the skeleton exactly as glTF defines it.
- **Mixed models:** a skinned model with an unskinned mesh, or more than one skin, is refused for now with a clear reason. Attaching unskinned meshes rigidly to a joint can come later.

## Decision

Proposed:

- **New functions:** `gltf::read_skinned` and `gltf::load_skinned` return a `SkinnedModel`:
  - **`model`:** the mesh in bind space, with parts and base colour materials as `read_model` gives them. The skinned mesh node's own transform is ignored, as glTF requires.
  - **`joints` and `weights`:** for each vertex, up to four skeleton joints and their weights. Weights are normalised to sum to 1. An unused slot has weight 0.
  - **`skeleton`:** the skin's joints and every node above them, parents first (by depth, then node index). Rest poses come from the nodes; a node matrix is split into translation, rotation and scale. Skin joints get the skin's inverse bind matrices.
  - **`clips`:** one `animation::Clip` per animation, named as in the file or "animation N". Channels for nodes outside the skeleton, and morph-target weights, are left out. Each clip must pass `animation::valid_clip`.
- **Forms read:**
  - joint indices as unsigned 8- or 16-bit;
  - weights and rotation keys as float or normalised 8- or 16-bit integers (signed or unsigned), as glTF allows;
  - LINEAR, STEP and CUBICSPLINE samplers.
- **Refused, each with a reason:**
  - no skin, or more than one;
  - an unskinned mesh in the scene;
  - a skinned primitive without JOINTS_0 and WEIGHTS_0, or with JOINTS_1;
  - joint indices outside the skin;
  - negative weights, or weights summing to 0;
  - more than 256 skeleton joints;
  - a skeleton node matrix that shears;
  - fewer inverse bind matrices than joints;
  - a joint listed twice;
  - node cycles;
  - unknown interpolations or paths;
  - clips that would not sample.
- **Tests share one builder:** the in-memory glTF builder moves to `tests/support/gltf_builder.hpp`, so the existing `assets.gltf` and the new `assets.gltf_skin` use it.

## Contracts and compatibility

- **New:**
  - `gltf::SkinnedModel`, `read_skinned` and `load_skinned`;
  - the `assets.gltf_skin` test;
  - `tests/support/gltf_builder.hpp`.
- **Unchanged:**
  - `read`, `read_model`, `load` and `load_model`, and their results;
  - `MeshData`.
- **Not yet:**
  - drawing skinned meshes (next PR);
  - more than four joints a vertex;
  - several skins;
  - unskinned meshes attached to joints;
  - morph targets;
  - sparse accessors (refused, as before).

## Consequences

- **What it enables:** the next PR can draw a character exported from Blender (or any glTF exporter) and play its animations.
- **Cost:** a model mixing skinned and rigid meshes is refused until rigid attachment is added. The reason names it.

## Verification

- **`assets.gltf_skin`** (new; models built in memory):
  - **the arm model:** an "Armature" node 5 m up holds a shoulder and an elbow joint, and the mesh node has a translation of its own:
    - **the mesh is in bind space:** the mesh node's translation is ignored;
    - **the skeleton:** Armature → shoulder → elbow, with their rest poses, the skin's inverse bind matrices, and the identity for the Armature;
    - **vertex joints:** mapped from the skin's list to the skeleton;
    - **weights:** normalised bytes 128 and 128 become exactly 0.5 each;
    - **at rest:** the skinned mesh stays put;
    - **"wave":** keeps only its skeleton channel (the mesh node's and the morph weights' channels are dropped). Its normalised 16-bit rotation keys, one with a negative part, turn the shoulder a quarter turn clockwise, which swings the forearm vertex down to (0, 3.5, 0);
    - **the unnamed animation:** is "animation 1"; its STEP sampler holds its first key mid-interval, and its CUBICSPLINE sampler follows its tangents;
    - **.gltf and .glb:** read the same;
    - **`read_model`:** still bakes the node transforms, as before.
  - **node matrices:** split into translation, rotation and scale; a mirror becomes a negative scale; shear is refused.
  - **flat-shaded skinned primitives:** keep their joints and weights per corner.
  - **refusals:** every case under Decision.
- **`assets.gltf`:** unchanged and passing, now with the shared builder.
- **Deliberate breaks:** 31 of 31 caught.
  - **Two first missed:** a misread signed 16-bit key, and STEP read as linear. The test then gained a rotation key with a negative part and a STEP sample mid-interval.
  - **Two replaced:** a uniform scale of byte weights, and a raised cycle limit, cannot change any result (weights are renormalised; the cycle was still refused). They were replaced by real faults: byte weights read as signed, and the cycle check removed. The second makes the reader hang, which the test's 60 s timeout catches.
  - **The rest:**
    - the mesh node transform not ignored;
    - ancestors left out;
    - joints not mapped;
    - weights not normalised;
    - negative and zero-sum weights accepted;
    - joint indices unchecked;
    - JOINTS_1 accepted;
    - inverse binds ignored, uncounted or read row by row;
    - parents after children;
    - duplicate joints;
    - mirror, shear and transposed rotation in matrix decomposition;
    - cubic read as linear;
    - unknown interpolations and paths accepted;
    - non-skeleton and morph channels kept;
    - clips not validated;
    - unnamed clips;
    - flat normals dropping skin data;
    - a second mesh without the skin, or with another skin, accepted;
    - the joint limit unchecked.
- **Independent continuation results (Codex):** exact clean source `15383a27bcc788191bb6646f5d01f27a4673ea47` passed 61/61 CTests each Linux Debug, ASan/UBSan and clang-tidy 18, no skips; installation/installed CLI passed and source stayed unchanged. Draft #55 makes the pushed source reviewable; Claude's branch was not changed. Windows remains unverified.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
