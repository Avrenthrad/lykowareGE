# ADR 0048 — Drawing the player, camera and crates between physics steps

Status: Proposed.
Related task/PR: Phase 2 PR 3 (branch `claude/tick-smoothing`), stacked on the playable character ([ADR 0047](0047-playable-character.md)).
Supersedes / superseded by: none. Uses the fixed-step clock's interpolation alpha ([ADR 0017](0017-clocks-fixed-step.md)).

## Context

- **Rates differ:** physics steps at a fixed 60 Hz (ADR 0017); frames come at whatever rate the display runs.
- **The problem:** the sandbox drew the latest step. On a 144 Hz monitor that means:
  - some frames show no movement at all;
  - others jump a whole step (4 m/s ÷ 60 = 6.7 cm);
  - because the follow camera moves with the player, the whole view judders.
- **Already flagged:** ADR 0047 lists this under "not yet". The sandbox's comment said "smoothing between ticks (interpolation) comes later".
- **What the clock offers:** `FixedStep::advance` already gives `FrameSteps::alpha`, how far real time has run past the latest step, "for drawing only".

## Options

- **Blend the last two steps by alpha (interpolation):** always one step (16.7 ms) behind, but never shows a state the simulation did not reach.
- **Predict past the latest step (extrapolation):** no delay, but it overshoots when the character stops or hits something, then snaps back.
- **Step physics at the display rate:** gives up the fixed step that determinism and replays rest on (ADR 0017).

Interpolation was chosen. It is what the clock's alpha is for.

## Decision

Proposed:

- **`PlayerCharacter`:**
  - `drawn_feet(alpha)` blends the feet before and after the latest step, with alpha clamped to 0..1; a non-finite alpha gives the latest step;
  - `view(seconds, alpha)` orbits the camera round the drawn pivot (drawn feet plus eye height);
  - alpha defaults to 1, the latest step, so existing calls behave as before;
  - `before_step()` records the feet before each step.
- **Presentation only:** alpha never reaches the simulation. Movement, jumps and the physics world are unchanged, and so are their hashes.
- **Sandbox:**
  - passes each frame's alpha to the player, the drawn body and the camera;
  - blends each crate's position (linear) and rotation (normalised linear blend along the shorter way) between the last two physics states;
  - only the drawn `Transform` changes, and the next step overwrites it from physics (`physics_ecs::Link::sync`).

## Contracts and compatibility

- **Changed, additions only:**
  - `PlayerCharacter::view` gains a defaulted `alpha` parameter;
  - new `PlayerCharacter::drawn_feet`.
- **Sandbox:** its crates and character are drawn one step (16.7 ms) behind the simulation.
- **The follow camera's probes:** they start from the drawn pivot. The drawn pivot always lies between two pivots the character really had, both inside its capsule, so the pivot stays in open space (ADR 0044's requirement).
- **Not yet:**
  - the drawn body's facing is not blended (it turns at once, as before);
  - Codex's scene presentation snapshots (ADR 0031) already blend scene entities; the sandbox's own crates do not go through scenes.

## Consequences

- **Smooth at any rate:** the player, the camera and the crates move evenly at any frame rate.
- **Cost:** input shows on screen one physics step (16.7 ms) later than before. Whether that is felt is the owner's call in play.

## Verification

- **`player.course`** gains `test_drawn_between_steps`:
  - **the blend:**
    - alpha 0 is the step before and alpha 1 the latest;
    - 0.5 is halfway;
    - alpha is clamped;
    - non-finite gives the latest;
    - the camera orbits the drawn pivot, and without an alpha the latest pivot;
  - **at 144 Hz over 60 Hz steps, walking at 4 m/s:**
    - the drawn feet and the camera each move 0.0278 m every frame (exactly 4 ÷ 144);
    - the latest step alone moves 0 m on some frames and 0.067 m on others.
- **The course bot:** now draws as the sandbox does, with each frame's alpha. Every camera check still passes against the drawn pivot: nothing between pivot and eye, near plane never in a block, tunnel pull-in on 95 of 98 frames.
- **Deliberate breaks:** 6 of 6 caught:
  - previous feet never recorded;
  - alpha ignored;
  - alpha not clamped;
  - a non-finite alpha drawing the step before;
  - the camera ignoring alpha;
  - the blend running backwards.
- **Not covered by a test:** the sandbox's crate blend. It is sample code that mirrors the tested player blend; `sandbox.play` runs it.
- **Local results:** 59/59 pass in the Debug, ASan/UBSan and clang-tidy presets, with no skips. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion` sweep finds nothing in the new code.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
