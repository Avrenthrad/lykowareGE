# ADR 0074 — Locomotion feel: one motor for players and NPCs

Status: Proposed.
Related task/PR: owner request of 12 November 2026 ("player and NPC locomotion feel"); first of five stacked PRs (branch `ccr-178f7225-atvbfx`), based on `main` after #81.
Supersedes / superseded by: none. Builds on ADR 0047 (playable character), 0048 (drawing between steps), 0062 (locomotion states and crossfades), 0064 (controller authority), 0066–0072 (foot placement, plants, heading, pelvis, balance).

## Context

- **What exists:**
  - the player moves at the full walk (4 m/s) or run (7 m/s) speed the instant a key is held, and stops the instant it is let go (ADR 0047 lists "acceleration and turning curves" as not yet done);
  - the body's facing is the way it last moved, with an optional turn-rate limit that is visual only (ADR 0064);
  - the sample picks idle, walk, run, jump, fall or land from the resulting speed (ADR 0062);
  - feet plant, hold their heading, and the pelvis follows them (ADRs 0067–0072);
  - physics has final authority over where the character is (ADR 0064), and drawing blends between physics steps (ADR 0048).
- **The owner's quality bar:** movement that is realistic, smooth, weighted and grounded, at the level of the best cinematic open-world games, and better in two named ways:
  1. **Sharper when called for.** A character can cut, stop and start sharply (combat, a nimble NPC), without sliding or snapping velocity: plant and pivot, quick braking, tight turns, feet planted throughout.
  2. **More fluid in direction.** Heading is continuous, not eight-way. Mid-stride redirects stay smooth. Strafe, diagonal and backward movement do not pop. The body is not locked into a committed turn. Stick input relative to the camera is followed closely.
- **One reference game is named by the owner as a feel target only** (see the legal line below). Nothing in this design depends on it.

## Options

- **Where the movement rules live:**
  - **inside `PlayerCharacter`,** which NPCs cannot use;
  - **a separate engine module that knows nothing about input, cameras or physics.**

  A separate module, `lykoware/locomotion.hpp`: a motor that turns "where I want to go and face" into "the velocity to ask physics for, and how the body is carried". The player feeds it from input; an NPC feeds it from its own decisions.
- **How "sharp versus heavy" is expressed:**
  - **separate hand-tuned parameter sets per character,** which drift apart;
  - **one agility number,** which hides too much;
  - **one agility number that sets every parameter by a documented rule, each of which can still be overridden.**

  The third. Agility 0 is heavy, 1 is sharp; the rule is in data, not code.
- **How a sharp redirect avoids sliding:**
  - **turn the velocity at once,** which is a slide: the feet cannot have done that;
  - **limit how fast velocity may change, and brake first when the turn is too tight to carry.**

  The second. This is plant and pivot: brake along the old heading, plant, push off along the new one. Sharpness comes from *how hard* the character may brake and push, never from skipping the braking.
- **Facing versus heading:**
  - **the body always faces where it moves,** which makes strafing and backing impossible;
  - **facing is its own input.**

  Its own input. With no facing given, the body turns toward where it moves. With one given (aiming, combat, a locked camera), it strafes and backs away while facing it.
- **How animation and movement stay together:**
  - **animation moves the character** (root motion in charge), which fights collision;
  - **physics moves the character and animation is played to match what actually happened.**

  Physics stays in charge (ADR 0064). The stride is played at the rate the character actually moved, so feet do not slide when a wall or slope slows it. Clips with root motion can later *suggest* a velocity through the same input.
- **Motion matching:** see the legal line. The motor and the clip interface below do not need it.
- **Where tuning lives:** ADR 0004 says hand-edited config is TOML and machine-written data is JSON. No TOML reader is vendored yet, and character profiles are already JSON. The parameters go in JSON now; a TOML reader is a separate dependency decision.

## Decision

Proposed. Each part below is its own PR; this ADR lands first, alone.

### The motor (PR 2)

- **`lykoware/locomotion.hpp`: `LocomotionMotor`.** Pure and deterministic: no input, camera, physics or animation types.
  - **In, each fixed step:** a wanted horizontal velocity (direction and speed), an optional wanted facing, whether the character is on walkable ground, and the velocity physics actually gave it last step.
  - **Out:** the velocity to ask physics for; the body's facing; the gait; a lean (how far the body tips into acceleration and turns); and what the legs are doing (moving, braking, pivoting, stopped).
- **Gaits:** idle, walk, jog, run, sprint. Each has a speed in data. The wanted speed picks the gait, with a margin so it does not flicker. An analog stick gives every speed in between.
  - Starting values, to be tuned in play: walk 1.6, jog 3.2, run 5.0, sprint 7.0 m/s.
  - The present 4 and 7 m/s stay available: a character with no locomotion data moves exactly as before.
- **Speed changes:** acceleration and braking limits in m/s², braking stronger than acceleration. Braking is in data separately for "let go" and "reverse".
- **Turning:** a turn rate in degrees per second that falls as speed rises (two points in data: at rest and at sprint). A turn tighter than the rate allows at the present speed is a pivot.
- **Plant and pivot:** when the wanted direction differs from the present one by more than the pivot angle, the motor brakes along the present heading to the pivot speed, turns, and pushes off. It reports the pivot so animation can plant the outside foot.
- **Agility, 0 to 1:** sets acceleration, both brakings, both turn rates, the pivot angle and pivot speed, the gait-change time and the lean, each by a straight line between a "heavy" and a "sharp" value held in data. Any one may be set directly instead.
  - A character has a default profile and may name others by state (for example `explore` and `combat`).
- **In the air:** the motor keeps the horizontal velocity and allows a small steering allowance from data; it does not brake.

### Directional fluidity (PR 3)

- **Continuous heading:** the wanted direction is any angle; nothing snaps to eight ways.
- **Strafe, diagonal, backward:** with a wanted facing, the motor reports the movement direction relative to the body (forward, back, left, right, as a continuous angle), and backward and sideways speeds are scaled by data.
- **Mid-stride redirects:** small changes steer the velocity within the turn rate without touching speed; only a turn past the pivot angle brakes.
- **Camera-relative input:** the player converts stick and keys to a wanted velocity from the camera's heading each step (as now), so turning the camera turns the request and the motor follows it at its turn rate.

### Animation (PR 4)

- **The clip interface:** a locomotion set in data lists clips by gait and by direction relative to the body, each with the speed it was authored at and its plant and lift events (ADR 0067). It takes procedural, authored or motion-capture clips alike.
- **Stride matching:** a clip is played at (actual speed ÷ authored speed), within limits, and gaits and directions are blended, so the feet's ground speed matches the body's.
- **Starts, stops and pivots:** the motor's leg state selects start, stop and pivot clips where the set has them, and falls back to blending where it does not.
- **Lean:** applied to the pelvis and spine from the motor's lean, bounded in data.
- **Feet:** the existing plants, heading lock, pelvis drop and balance shift run on top, unchanged in contract.
- **Placeholder clips are acceptable; a placeholder motor is not.**

### NPCs (PR 5)

- An NPC owns a motor and a physics character and feeds the motor a wanted velocity and facing from its own logic. It uses the same data format, with its own agility.

### What is measured (from PR 2 on)

- **No slide:** a planted foot's world movement per step, and the mismatch between stride speed and body speed, each under a bound in data.
- **Frame-rate independence:** the motor runs only on fixed steps; the same inputs at 30, 60 and 144 Hz frame rates give bit-identical motor states.
- **Sharp and heavy differ as specified:** stopping distance from a run, time to reverse, turning circle at a jog, and time from idle to run, each compared between agility 0 and 1 against the data.
- **Direction-change fixture:** a fixed sequence of redirects (45°, 90°, 135°, 180°, a figure of eight, a strafe box) run in the test and playable in the sandbox course.

## The legal line

- **The named game is a feel and quality target only.** Nothing from it or any other game is copied, extracted, ripped or recreated: no assets, animations, motion capture, audio, code, data or names. Nothing derived from it ships.
- **Clips:** only original clips, or clips under a permissive licence recorded with the asset (ADR 0000).
- **Techniques:** only publicly documented ones.
- **Motion matching is stopped for legal review.** This is a record of what was found, not legal advice.
  - The basic technique (searching a database of clip frames for the best match to the present pose and wanted path) was described publicly by Büttner and Clavet (nucl.ai 2015) and Clavet (GDC 2016), and extended by Holden and others ("Learned Motion Matching", SIGGRAPH 2020).
  - A search on 12 November 2026 found granted United States patents in this area assigned to Electronic Arts, including US 11,562,523 ("Enhanced animation generation based on motion matching using local bone phases") and US 12,403,400 ("Learning character motion alignment with periodic autoencoders"). Their claims were not analysed.
  - Whether a plain motion-matching search is free to use has not been established. No database search of that kind is implemented until the owner's patent review says so. No phase-based or learned variant is implemented.
- **Everything in "Decision" above is ordinary kinematics** (limited acceleration, turn rates, blending clips by speed and direction, playing a clip at a rate) and does not depend on that review.

## Contracts and compatibility

- **PR 1 (this ADR):** documentation only.
- **Later PRs, additions only:**
  - `lykoware/locomotion.hpp` (motor, gait, agility profile and its JSON reader);
  - `PlayerConfig` gains an optional locomotion profile; without one, movement is exactly as today, and `player.course` stays as it is;
  - character profiles (version 1 JSON) gain an optional `locomotion` object, refused when malformed;
  - the sample's locomotion selection and clip playback move onto the motor's outputs.
- **Determinism:** the motor uses the same inputs in the same order to give bit-identical results in one build on one machine, as `PlayerCharacter` does now (ADR 0047's note on `sin`/`cos`/`atan2` still applies across platforms).
- **Authority:** unchanged. Physics resolves collisions, slopes and steps; the motor only asks.
- **Drawing between steps:** unchanged. Facing, lean and feet are blended between the last two steps like the position.

## Consequences

- **The player and every NPC move by the same rules,** and a heavy brute and a nimble scout differ by data.
- **Feel becomes testable:** stopping distances, turning circles and foot slide are numbers a test can hold.
- **The default walk slows to a human pace.** Courses and tests that assume 4 and 7 m/s keep those through the unchanged no-profile path until they are retuned.
- **Final feel is still the owner's call** in play, on the RTX 4070 desktop (02.15, 02.16). Numbers here are starting points.
- **A real clip set (authored or captured) is still needed** for the bar to be met on screen; until then the procedural mannequin clips stand in.

## Open questions for the owner

- **Gait speeds:** are walk 1.6, jog 3.2, run 5.0 and sprint 7.0 m/s the right starting points?
- **Default agility:** 0.5 for exploration and 0.85 for combat, unless told otherwise.
- **TOML:** vendor a small permissive TOML reader so hand-tuned profiles can be TOML (ADR 0004), or keep them in JSON?
- **Motion matching:** send to patent review, or drop it in favour of the blended clip set above?

## Verification

Planned, per PR; results are recorded in STATUS as each lands:

- **PR 1:** links and consistency of this ADR and the index.
- **PR 2:** a new `locomotion.motor` test (speed changes, braking, turn rate by speed, gaits and their margins, pivots, agility mapping, air, refusals, bit-identical repeats, zero allocations) and `player.course` extended with the measured feel checks above.
- **PR 3:** the direction-change fixture in the test and the sandbox course.
- **PR 4:** stride matching and no-slide measured on the mannequin and the imported player; rendered checks in `--play`.
- **PR 5:** an NPC and the player on the same course with different agility.
- **Every PR:** deliberate breaks; the Debug, ASan/UBSan and clang-tidy presets; the five hosted jobs.
