# ADR 0082 — A stated directional light

Status: Proposed.
Related task/PR: `07.03a`, the first slice of `07.03`, branch `claude/07.03a-stated-light`.
Supersedes / superseded by: none. Builds on ADRs 0025 and 0026 (the lit shader and the first renderer).

## Context

- **What exists:** the lit shader hard-codes one light, `normalize(0.4, 1, 0.3)`, and an ambient of 0.25.
- **The need:** `07.03` covers lighting, shadows, materials, transparency and post. It is too large for one PR, so it is split into lettered children: `07.03a` light, `07.03b` shadows, `07.03c` materials, `07.03d` transparency, `07.03e` post. This ADR covers `07.03a`: a light the game states.
- **Constraints:**
  - The push-constant block is full at 128 bytes, the guaranteed Vulkan minimum.
  - The RHI has no uniform buffers yet.
  - Existing images must not change.

## Options

- **Add a uniform buffer to the RHI.** This is new GPU resource management, and it belongs with `07.04`.
- **Use the four floats that were unused in the push-constant block.** These are the w of the three normal columns and colour.a. They fit a direction and an ambient, but not a light colour.

The second option. It needs no new GPU resource, and the first slice stays small. Light colour and more than one light wait for the uniform buffer in `07.04`.

## Decision

- **API:** `render::Lighting { direction, ambient }`, `Renderer::set_lighting` and `Renderer::lighting` in `render.hpp`.
- **The formula:** colour × texture × (ambient + (1 − ambient) × max(0, n · normalize(direction))).
  - The default is direction (0.4, 1, 0.3) and ambient 0.25.
  - With the default, a renderer that never calls `set_lighting` draws byte for byte as before.
- **Packing:**
  - The direction goes in `normal[0..2].w`. The vertex shaders read only xyz.
  - The ambient goes in `colour.a`.
  - Both `lit.slang` and `skinned.slang` read them the same way.
- **GPU resources:** none new. The renderer owns its lighting as plain CPU state and writes it into each draw's push constants. Lifetime and threading follow the renderer (ADR 0026).
- **Refusals:** `set_lighting` returns false with `InvalidArgument` and leaves the lighting unchanged for:
  - a direction that is not finite or has zero length;
  - an ambient outside 0..1, or not finite.
- **A missing or stale shader:**
  - A missing shader: `Renderer::create` already fails, as before.
  - A stale SPIR-V file built before this change would ignore the stated light. `render.lighting` catches that, because a moved light must change the image.

## Consequences

- Games can set the sun's direction and the ambient level. Light colour, point lights and shadows come in later slices.
- Any code that fills the lit shader's push constants by hand must now fill the four extra floats. `rhi.mesh` does this.

## Validation

`render.lighting` draws a white cube on lavapipe. It skips with exit code 77 when there is no Vulkan; it never passes without drawing.
- **Formula check:** the centres of the top and front faces match the formula within 2/255 under five lights: the default, from above, from in front, from below with no ambient, and a diagonal with ambient 0.6.
- **Default:** a renderer that never set its lighting gives exactly the default image.
- **Deliberate break:** if the light were ignored, moving it would not change the image, and the light from below would not leave the faces black. Both are checked.
- **Refusals:** a zero direction, a NaN direction or ambient, and an ambient of −0.1 or 1.1 are refused, and the lighting is left unchanged.
