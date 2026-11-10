# Authored walking foot plants

ADR0067 adds position anchors to the existing opt-in foot IK. The engine `FootPlant` owns no world/body lifetime. It probes on a fresh plant, stores an ankle point and normal in support-local coordinates, and resolves them through the body's current double-precision position/rotation. Holding the phase does not re-probe or slide the anchor to the animated foot. Lift re-arms it. Missing/steep support, destroyed/reused/cross-world handles and excessive distance release it until a new cycle or explicit reset. Invalid calls preserve state/output. Successful calls use fixed storage and read-only physics queries.

## Authoring

A character profile with the existing seven direct-chain bone mappings can add:

```json
{
  "animation_graph": "walk.graph.json",
  "foot_ik": true,
  "foot_locking": true,
  "foot_max_plant_distance": 0.35,
  "foot_align_normals": true,
  "foot_pelvis": true
}
```

This snippet augments the model and bone mappings; it is not a complete profile. Locking requires foot IK and an authored graph with all four marker names. Distance is finite 0.05..1 world metre, default0.35. Inspection reports effective policy. Existing profiles remain unchanged unless opted in.

For a one-second looping clip named `walk`, a graph state could contain:

```json
{
  "name": "gait", "clip": "walk", "loop": true,
  "events": [
    {"name": "left_plant", "seconds": 0},
    {"name": "right_lift", "seconds": 0},
    {"name": "left_lift", "seconds": 0.5},
    {"name": "right_plant", "seconds": 0.5}
  ]
}
```

Marker placement/rate must fit the actual gait; no automatic gait inference exists. The graph is generic; the sample interprets these four names. Playback consumes time-zero markers before first rendering, retains phases between updates and clears them on transitions/reset. Per-foot cycle epochs carry lift/replant edges across frames even when both events occur during one update. At loop boundaries, the sample applies outgoing end markers before incoming zero markers at the same absolute time, preserving the generic graph ordering for other consumers. Phase changes then force a fresh anchor; they do not accidentally continue an old planted foot.

While grounded, moving and visible, the sample applies IK only to planted legs; swing legs retain animation. Stationary playback uses the prior stance path. Material reach error above2mm releases a lock instead of stretching or claiming an unreachable target. At most two releases trigger bounded full bilateral retries; body/pose/contact/statistics commit together. Normal alignment and bounded pelvis behavior retain ADR0066's policy. Pelvis correction requires both contacts; single-foot phases do not add body balance. Air, invisibility, respawn, teleport and invalid/long-delta paths reset anchor state.

Anchors hold position and surface normal. With `foot_heading_lock` they also hold the foot's heading (ADR 0068): the animated heading at the plant, flattened onto the support and carried with it, which the sample turns the foot back to about the support normal, by at most `foot_max_heading_degrees` (default 60). They follow support translation/rotation. A caller that lists where moving supports are drawn this frame (`DrawnBody`, ADR 0069) has held feet resolved, and fresh plants anchored, against those drawn poses, so feet stay still on supports as drawn between physics steps; the sandbox lists its crates. Unlisted supports use their physics state. Balance and production gait tuning remain separate work. Artist-calibrated clearance is initially vertical world height; the acquired support-local offset is thereafter rigidly transported with that body.

## Drive the actual player headlessly

```text
lyko run -- --window --offscreen --vulkan --play --no-audio --character-profile hero.character.json --play-input walk.input.json
```

The explicit `--` forwards sample arguments through the owned process launcher without a shell. Existing `lyko run --scene ...` remains the scene-command path.

`walk.input.json`:

```json
{"version":1,"frames":[{"repeat":30},{"forward":0.1,"repeat":90}]}
```

Each expanded entry supplies one PlayerIntent at a fixed60Hz sample delta. Moves are -1..1, yaw/pitch increments ±pi radians, zoom ±4; jump/run are strict booleans. Defaults are zero/false. Repeat duplicates the whole intent, including repeated jump presses. At most6000 expanded frames,64KiB and nesting below8 levels; unknown/duplicate/malformed fields are rejected. The replay requires `--play --offscreen --vulkan`; optional `--frames` must equal the expanded length. It uses the real input→player→Jolt→animation→IK→render path. This controlled simulation driver does not measure physical-PC frame pacing.

## Verification and limits

`animation.foot_plant` covers real static/kinematic supports, transport/tilt, stale/world lifetimes, suppression/re-arm, far-origin submillimetre motion, numeric refusals, controller reach/rollback, graph entry/wrap/reset/hidden-cycle markers, replay parsing and successful-update allocation counts. `sandbox.imported_player` runs an original glTF through real moving player replay both directly and through lyko, requiring reproducible plant/lift/applied-palette counters. `render.gpu_skinning` independently compares the held-foot pose's GPU pixels with CPU-deformed geometry and saves `gpu-skinning/walking-foot-plants.ppm`.

These are runtime correctness fixtures, not finished game art, target-PC acceptance or a crowd/whole-engine allocation budget. Full source/native/hosted/review evidence is recorded in STATUS and loop progress. Unmerged work stays separate from main acceptance.
