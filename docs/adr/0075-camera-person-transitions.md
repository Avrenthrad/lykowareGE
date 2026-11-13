# ADR 0075 — First- and third-person camera transitions

Status: Proposed
Related task/PR: 02.14
Supersedes / superseded by: refines ADR 0044

## Context
`FollowCamera` (ADR 0044) switches between first person (distance 0) and third person with `set_distance`. A shorter distance is taken at once, so going to first person pops the eye several metres in one frame. 02.14 asks for a transition that neither pops nor passes through a wall, while today's behaviour stays when the new path is off.

## Options
- Ease `current_distance` toward the requested distance at a fixed speed, keeping wall pull-in immediate. Small, testable, no new type.
- A separate camera-mode blender over two cameras. More general, but a second camera to keep out of walls, and combat cameras (Grok's 03.03) would build on it before it is needed.

## Decision
- `FollowCameraConfig::transition_speed` (m/s, default 0). At 0 nothing changes from ADR 0044.
- Above 0, `set_distance` with a different value (after the first update) starts a transition. Each update moves the eye along its line of view toward the new distance by at most `transition_speed × seconds`, in either direction. The view direction does not change, because first person looks along the same line from the pivot.
- Walls keep priority: the probes reach out to the farther of the current and requested distance, and the eye is pulled in at once to stay `clearance` short of any hit. A transition out stops at the wall and ends there.
- `FollowCamera::transitioning()` reports whether one is in progress. Non-finite or negative speeds read as 0.
- Presentation only, one thread, as ADR 0044. No physics state changes; replays are unaffected.

## Consequences
- The sample may set a transition speed; until it does, play is unchanged.
- Combat lock-on (03.03) stays Grok's and is not part of this camera.

## Verification
`camera.follow` (`test_transitions`): steps bounded by speed × dt both ways, no view turn, a wall behind stops the transition short of it, a wall appearing mid-transition pulls in at once, and with the speed at 0 the same change is a one-frame pop (the deliberate break).
