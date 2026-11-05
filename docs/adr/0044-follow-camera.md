# ADR 0044 — A follow camera that never looks through walls

Status: Proposed.
Related task/PR: Phase 2 PR 1 (branch `claude/follow-camera`), stacked on the capture golden ([ADR 0043](0043-renderer-golden-capture.md)).
Supersedes / superseded by: none. Uses the physics ray cast ([ADR 0020](0020-physics-api.md)) and the renderer's camera ([ADR 0026](0026-renderer.md)).

## Context

Phase 2 is "character feel on terrain". Its deliverables include camera obstruction handling, and its exit asks for "no ... camera-through-wall failures". The engine has a physics character controller that walks, climbs slopes and steps, and jumps (`physics.character`). It has no camera that follows a player.

## Options

- **Obstruction test:**
  - **ray probes** against the physics world;
  - **a sphere cast;**
  - **ignore obstruction and fade walls instead.**

  Ray probes were chosen. The physics API already has `cast_ray`. A sphere cast would need a new physics query. Five rays approximate one well enough to keep the near plane out of walls, and the tests check exactly that.
- **Where the rays start:**
  - **all from the pivot,** fanning out to the camera's place and to four points `clearance` across the view from it;
  - **parallel rays from points around the pivot.**

  The fan was chosen. Every ray starts in open space at the pivot, so a character standing against a wall does not start a ray inside it and collapse the camera for no reason.
- **Movement:**
  - **pull in at once, ease back out;**
  - **ease both ways;**
  - **snap both ways.**

  Pull in at once, ease out was chosen.
  - Easing in lets the view pass through a wall for a few frames; a deliberate break shows exactly that.
  - Snapping out jumps the view whenever an obstacle clears.

## Decision

Proposed:

- **`lykoware/follow_camera.hpp`:** `FollowCamera` orbits a pivot (a character's head, say) at a yaw and pitch the player turns. `update()` returns a `render::Camera`.
  - **Distance:** 4 m by default. `set_distance(0)` is first person: the eye is at the pivot, looking forward.
  - **Pitch:** clamped to -0.6..1.3 rad; positive looks down from above. Yaw wraps to [-π, π). Non-finite turns are ignored.
  - **Probes:** each update casts five rays from the pivot, each reaching `clearance` (0.2 m, at least the near plane) past the camera's place. The camera stops `clearance` short of the nearest hit. It never comes closer than `min_distance` (0.3 m).
  - **Movement:** it pulls in at once and eases back out at `return_speed` (6 m/s).
  - **Movement directions:** `forward()` and `right()` are the camera's horizontal axes, for camera-relative movement input.
- **Presentation only:** it reads the physics world and changes nothing. Characters are not physics bodies, so the player's own character never blocks the view.

## Contracts and compatibility

- **New:** `lykoware/follow_camera.hpp`, `engine/src/follow_camera.cpp`, the `camera.follow` test.
- **Unchanged:** existing APIs.
- **Requirement:** the pivot must be in open space. The caller places it inside the character, above its feet.
- **Not yet:**
  - the sandbox using it, with a playable character (Phase 2 PR 2);
  - mouse-look input (`InputState` has positions, not deltas);
  - smoothing of the pivot;
  - collision with characters other than the player's;
  - sphere casts.

## Consequences

- Phase 2's camera obstruction deliverable has an engine module with a headless test of its exit criterion. Whether that counts as done is the owner's call after seeing it in play (PR 2).

## Verification

- **`camera.follow`** (new, physics only):
  - **Open ground:** keeps its 4 m distance and looks at the pivot. Forward and right are -Z and +X at yaw 0, horizontal and orthogonal.
  - **A wall behind:** pulls in to exactly the clearance short of the wall along its pitched line. The eye stays on the pivot's side and the near plane out of the wall. When the wall is removed it eases out no faster than 6 m/s, back to 4 m.
  - **A wall just past the full distance:** at 4.05 m, the camera still keeps 0.2 m from it.
  - **Turned all the way round** (72 yaws × 7 pitches) in a room with two walls, a crate and a low ceiling:
    - no wall is ever between the pivot and the eye;
    - the eye never comes within the near plane of any wall;
    - it pulls in for many views but not all.
  - **A wall beside the character**, running along the view, does not pull it in.
  - **Controls:** pitch clamps, yaw wraps, non-finite turns are ignored, and distance 0 is first person looking forward.
- **Deliberate breaks:** 9 of 9 caught:
  - no obstruction handling;
  - only the centre ray;
  - no clearance kept;
  - the probe stopping at the camera;
  - snapping back out;
  - easing in instead of snapping;
  - pitch unclamped;
  - forward reversed;
  - first person looking backwards.

  "The probe stopping at the camera" was missed at first; the wall-just-past-the-camera case was added for it.
- **Local results:** to be recorded.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
