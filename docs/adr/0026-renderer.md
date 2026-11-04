# ADR 0026 — The first renderer: lit ECS scenes, camera-relative

Status: Proposed.
Related task/PR: 3D rendering, PR 3 (branch `claude/render-scene`), stacked on PR 2 ([ADR 0025](0025-rhi-meshes.md)) and PR 1 ([ADR 0024](0024-math.md)).
Supersedes / superseded by: none. Moves `Transform` out of the physics link ([ADR 0023](0023-ecs.md) planned this). The sandbox now deploys the engine's lit shader instead of its triangle shader ([ADR 0016](0016-runtime-shader-files.md)).

## Context

The owner chose falling crates as the first 3D milestone: physics, the ECS and rendering working together, checked in CI. The RHI can draw meshes (ADR 0025), but nothing yet turns ECS entities into draws, the window path has no depth, and the sandbox still draws its test triangle.

## Options

- **Where draws come from:**
  - the renderer reads the ECS directly (each `Transform` plus `MeshRenderer`);
  - game code builds a draw list itself.

  Reading the ECS was chosen, because it is what game code wants. A draw list can be added under it later without changing callers.
- **Meshes:** built-in shapes now (a cube), or the asset pipeline (glTF, ADR 0004) first. Built-in shapes were chosen; loading meshes comes with scene loading.
- **Precision:** camera-relative drawing from the start (ADR 0021, ADR 0024). Adding it later would touch every draw.

## Decision

Proposed:

- **`lykoware::Transform`** (`lykoware/transform.hpp`): a double-precision position and a rotation. `physics_ecs::Transform` is now an alias of it. Physics writes it and the renderer reads it.
- **`lykoware::render`** (`lykoware/render.hpp`):
  - **`MeshRenderer`** component: a built-in shape (`MeshShape::Cube`, 1 m on a side), a scale and a colour.
  - **`Camera`:** eye and target as world positions in double precision, up, field of view and near distance.
  - **`Renderer`:**
    - `create(device, colour format, lit vertex SPIR-V, lit fragment SPIR-V)` builds the pipeline (position and normal, reversed depth, back-face culling, 128-byte push constants) and the cube buffers;
    - `draw(entities, camera, target or swapchain, clear)` makes one draw per entity with `Transform` and `MeshRenderer`, in ECS iteration order (deterministic).
  - **Every position is made relative to the camera before it becomes float.** A scene 16 km away renders exactly like the same scene at the origin.
- **Shaders:** `engine/shaders/lit.slang` is the engine's lit shader: one directional light from above plus ambient. Programs compile it at build time under their own file names and pass the SPIR-V to `Renderer::create`; the sandbox's are `sandbox_lit.*`.
- **Swapchains:**
  - `SwapchainConfig::depth` keeps a depth buffer the size of the swapchain, rebuilt with it;
  - `render_meshes()` draws meshes into the window with the same checks as `RenderTarget::draw_meshes`. The checks are now shared code.
  - One depth image serves every frame in flight: each frame's depth barrier waits for the previous frame's depth writes.
- **Sandbox:** `--window --vulkan` now shows the falling-crates scene:
  - a grey ground and a 5 × 5 grid of coloured crates that tumble down, settle, and are replaced every 8 seconds;
  - the physics link destroys their bodies, and the ECS destroys the entities.

  The simulation runs in fixed 60 Hz ticks (ADR 0017). Frames show the latest tick; smoothing between ticks comes later.

## Contracts and compatibility

- New public headers `lykoware/transform.hpp` and `lykoware/render.hpp`. New `SwapchainConfig::depth`, `Swapchain::has_depth()` and `render_meshes()`.
- `rhi::MeshDraw` moved from `render_target.hpp` to `buffer.hpp`. `render_target.hpp` includes `buffer.hpp`, so callers are unchanged.
- **Lifetime:** destroy a swapchain the renderer drew to before the renderer, since the swapchain's destructor waits for the GPU. The physics world outlives the ECS world (ADR 0023).
- The sandbox's shader files are now `sandbox_lit.vert.spv` and `sandbox_lit.frag.spv`. `sandbox.missing_shaders` checks the new name.
- Not yet:
  - meshes from files;
  - materials and textures;
  - shadows;
  - more lights;
  - interpolation between ticks;
  - frustum culling;
  - instancing;
  - window screenshots (the Phase 1 screenshot item).

## Consequences

- The first visible 3D: physics-driven, lit, depth-correct, the same at 16 km as at the origin.
- One draw call and one push-constant block per entity. Measurements decide when instancing is needed.

## Verification

- **New test `render.scene`** (lavapipe; skipped where there is no Vulkan): three crates drop onto a grey ground through physics and the ECS, and a wall stands behind them.
  - The crates land on the ground.
  - Each crate shows in its own colour where it landed.
  - The wall shows behind them. It is drawn after the crates, so they are in front only if depth works.
  - The ground in front is lit grey, and the sky keeps the clear colour.
  - Five objects are drawn.
  - Two runs give identical bytes.
  - The whole scene 16 km from the origin gives exactly the same image.
  - Drawing into a target without depth is refused.
  - No Vulkan validation errors.
  - The test writes the frame to `build/<preset>/render-scene/scene.ppm` for a person to look at.
- **Deliberate breaks:**
  - **Caught:** a renderer pipeline without depth; colour channels swapped; scale ignored; the camera's view ignored.
  - **Not caught here, as expected:** culling off. With depth on, a solid box's back faces are hidden anyway, so the image does not change; `rhi.mesh` tests culling directly.
- **A weakness found and fixed:** before the wall was added, turning depth off did not change this test's image, because nothing overlapped.
- **Sandbox:** `sandbox.vulkan_window` (headless window on lavapipe) and `sandbox.missing_shaders` pass. A local run of 600 frames drew 26 objects with no validation errors.
- **Local results:** 38/38 pass in the Debug, ASan/UBSan and clang-tidy presets.
- **CI:** the first run on 1e402d1 failed both Windows jobs. MSVC (warnings as errors) rejected a local `depth_view` in `swapchain.cpp` that hid the member of the same name (C4458); GCC does not warn about this without `-Wshadow`. After the rename, #34 [run 41823030117](https://github.com/Avrenthrad/lykowareGE/actions/runs/41823030117) on eb3716a passed all five jobs. A `-Wshadow` and `-Wconversion` sweep of every file the stack touches is clean.
