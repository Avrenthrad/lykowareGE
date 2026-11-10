# ADR 0069 — Feet stay still on moving supports as drawn

Status: Proposed.
Related task/PR: Phase 2 checklist 02.11c, the "moving-support render interpolation" part (branch `claude/drawn-support-feet`), stacked on the heading lock (#75, ADR 0068).
Supersedes / superseded by: none. Extends ADR 0067's support anchors; uses ADR 0048's drawing between steps.

## Context

- **How things are drawn (ADR 0048):** physics steps at a fixed 60 Hz. Each frame draws the actor and the crates blended between their last two steps by the clock's alpha, so everything on screen is up to one step behind the simulation.
- **How planted feet were resolved (ADR 0067):** through the support's latest physics state, not its drawn pose.
- **The visible problem:** on a moving support the planted foot is drawn ahead of the support by up to one step of the support's motion, and the lead changes with alpha from frame to frame. The foot slides back and forth on the support. On a platform moving at 1.5 m/s that is up to 2.5 cm a frame at 60 Hz steps. The same gap lifts or sinks a standing foot on a lift.
- **Checklist:** 02.11c stays partial until this and the heading lock (ADR 0068) are merged.

## Options

- **Where the drawn pose comes from:**
  - **the plant records each support's state before every step** and blends it by alpha, like `PlayerCharacter::before_step`;
  - **the plant reconstructs the previous state from the body's velocity;**
  - **the caller supplies the drawn poses** it is already drawing.

  The caller supplies them. Whoever draws a body already holds its two states and its blend (the sandbox's crates, `Scene::presentation`), so the foot then matches the drawn support by construction. Recording inside the plant would need a second, separate tick hook and still miss a support's previous state at the moment of a fresh plant. Velocity reconstruction is exact only for kinematic bodies; contacts move dynamic bodies without matching velocity.
- **How the caller supplies them:**
  - **a virtual lookup interface;**
  - **a span of `{body, position, rotation}` entries.**

  The span was chosen: plain data, no virtual calls, no allocation, and a caller lists only bodies that move. A linear search is enough for the few moving bodies near an actor. Unlisted bodies, such as static terrain, use their physics state, which is where they are drawn.
- **A fresh plant on a listed support:**
  - **anchor through the physics pose,** which puts the foot off its drawn plant point by the gap;
  - **carry the probe hit to the drawn pose.**

  The hit is carried: its point and normal move with the body from its physics pose to its drawn pose, then the point moves along the drawn tangent plane to straight below the foot. This is exact for flat surfaces and off by a second-order amount on curved ones. The probe itself still runs against the physics world.

## Decision

Proposed:

- **Engine (`lykoware/foot_plant.hpp`):**
  - `DrawnBody { BodyId body; DVec3 position; Quat rotation; }`;
  - `find_drawn(span, body)`: the first entry for a body, or null;
  - `drawn_hit(hit, physics state, drawn, from)`: the hit as drawn, or nullopt for non-finite input or a zero rotation;
  - both `FootPlant::update` overloads take a defaulted trailing `std::span<const DrawnBody> drawn`;
  - a held support that is listed is resolved through its drawn pose (target, normal and heading); a fresh plant on a listed support is anchored through it;
  - the physics world still decides whether the support exists: a destroyed support is lost even while listed;
  - a listed pose that is used must be finite with a non-zero rotation (it is normalised), or the call is refused with state and output kept; entries for other bodies are not checked;
  - `FootPlantResult::drawn` says the drawn pose was used;
  - unlisted supports and an empty list give exactly the previous results;
  - successful calls still allocate nothing.
- **Sample controller (`samples/course/foot_placement.hpp`):**
  - `update` takes the same defaulted list and passes it to its plants;
  - stance probes on a listed support are carried to its drawn pose with `drawn_hit`;
  - a `drawn` count joins the foot diagnostics;
  - presentation only, as before.
- **Sandbox:**
  - the crate scene lists each crate's drawn pose as it blends it;
  - the player's feet receive that list each frame;
  - the foot diagnostics report `drawn` (feet resolved on a drawn support) and `drawn_offered` (frames where the list was not empty).
- **No profile key:** this corrects where feet are drawn rather than adding a policy, so it applies whenever a caller lists drawn poses.

## Contracts and compatibility

- **Additions only:**
  - `DrawnBody`, `find_drawn`, `drawn_hit`;
  - a defaulted trailing parameter on both `FootPlant::update` overloads and on the sample's `Controller::update`;
  - `FootPlantResult::drawn`;
  - two diagnostics fields.
- **Unchanged without a list:** existing callers compile and behave exactly as before; the tests compare results bit for bit.
- **Callers must list the pose they draw.** A list that disagrees with the renderer moves feet off the drawn support.
- **Repeatability:** presentation only; drawn poses depend on the frame's alpha, so foot release decisions on a listed support can differ between frame rates. The simulation is untouched.
- **Not yet:**
  - balance and full-body constraints (02.11d);
  - moving platforms in the sandbox course (only falling crates are listed today);
  - physical-PC frame pacing and the owner's feel review.

## Consequences

- **Planted feet stay still on a moving support as drawn,** whatever the frame rate, and standing feet meet a lift where it is drawn.

## Verification

- **`animation.drawn_support`** (new):
  - **engine:**
    - a drawn hit on a tilted, moved and turned support matches a real probe of the same box placed at the drawn pose (within 1e-5), straight below the probe;
    - drawn at its physics pose, a hit is unchanged;
    - refusals: non-finite position or rotation, zero rotation, non-finite probe origin;
    - `find_drawn` takes the first entry and finds no unlisted body;
    - on a platform moving sideways at 1.5 m/s and on one rising at 0.6 m/s, drawn at four alphas per step for six steps, a planted foot stays still on the support as drawn (within 1e-6), and a fresh plant lands straight below the foot on the drawn top;
    - positive control: through the latest physics state the same foot slides by more than 0.7 of a step's motion;
    - on a turning support, the foot's place, normal and heading turn with it as drawn (within 1e-5);
    - unlisted supports, unusable entries for other bodies and an empty list match the old results exactly;
    - refusals keep state and output; a destroyed support is lost while still listed;
    - 1,000 updates with an eight-entry list allocate nothing;
  - **controller (stance rig, real Jolt):**
    - standing on a lift drawn 5 cm below its physics top gives the same ankles as a still floor at the drawn height (within 1e-5); positive control: without the list the feet stand more than 2 cm higher;
    - both feet planted on a platform moving at 1.5 m/s, the actor riding it as drawn: the ankles stay still on the platform as drawn (within 1e-4); positive control: without the list they slide by more than 0.7 of a step.
- **`sandbox.imported_player`:** the real walking replay reports `drawn` and offers the crates' drawn poses on every frame (`drawn_offered` above 0), with no failures.
- **Deliberate breaks:** 17 of 17 caught:
  - **engine (11):** the held drawn pose ignored; a fresh plant anchored through the physics pose; the probe hit not carried to the drawn pose; no tangent-plane projection; the normal not turned; the physics rotation not inverted; no validation of a used pose (first written in a form that did not compile, then re-run in one that did); `find_drawn` never matching; a listed support surviving physics loss; `result.drawn` never set; the heading turned by the physics rotation;
  - **controller (4):** the list not passed to the plants; stance probes not carried; walking and stance `drawn` counts not kept;
  - **sandbox (2):** crates not offered to the feet; the drawn list not filled.
- **Local results:** the full preset runs follow in the next commit.
