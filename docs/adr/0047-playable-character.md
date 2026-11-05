# ADR 0047 — A playable character and the obstacle course it is tested on

Status: Proposed.
Related task/PR: Phase 2 PR 2 (branch `claude/playable-character`), stacked on the follow camera ([ADR 0044](0044-follow-camera.md)).
Supersedes / superseded by: none. Uses the physics character ([ADR 0020](0020-physics-api.md)), the input snapshot and window ([ADR 0040](0040-window-lifecycle.md)) and the fixed simulation step ([ADR 0017](0017-fixed-step-clock.md)).

## Context

- **Phase 2 goal:** "controllable first-/third-person character with convincing grounded motion".
- **Phase 2 exit, in part:** "fixed-seed obstacle/slope/step/fall fixtures complete; no persistent ground penetration or camera-through-wall failures".
- **What existed:**
  - a physics character that walks, climbs slopes and steps, and jumps;
  - a follow camera (ADR 0044);
  - no way for a person to steer either.
- **Missing input:** `InputState` had mouse positions but no motion, and the window could not hold the mouse, so mouse-look was impossible.

## Options

- **Where the controls live:**
  - **in the sandbox only;**
  - **an engine module** (`lykoware/player.hpp`).

  The engine module was chosen. A test can then drive exactly the code a person plays with, and later games reuse it.
- **Mouse-look:**
  - **relative mouse mode** with per-frame motion;
  - **look only while a button is held,** using position changes.

  Relative mode was chosen. The cursor would otherwise stop at the screen edge.
  - Click to hold the mouse; Esc lets it go.
  - The platform releases it while the window is out of focus.
- **Jumps:**
  - **pass the jump straight to the world;**
  - **keep it in the player for a short buffer** and pass it only when the character stands on walkable ground.

  The buffer was chosen, for two reasons:
  - The world keeps an unused jump until the next landing, so a press in mid-air would fire on landing, however long ago it was pressed.
  - A press in a frame without a physics step would otherwise be lost.
- **The course the test runs:**
  - **written into the test;**
  - **a header shared with the sandbox** (`samples/course/obstacle_course.hpp`).

  The shared header was chosen. The course a person plays is then the course the test runs.

## Decision

Proposed:

- **`lykoware/input.hpp`:** `MouseState` gains `move_x` and `move_y`.
  - They hold the mouse motion of the frame, from SDL's relative motion.
  - They are cleared by `begin_input_frame`.
  - `note_mouse_motion` adds to them and ignores non-finite values.
- **`lykoware/window.hpp`:** `Window::set_relative_mouse(bool, error)` and `relative_mouse()`.
- **`lykoware/player.hpp`:** `read_player_intent(input, config, seconds, mouse_look)` reads one frame's intent from keyboard, mouse and gamepad together.

  | Action | Keyboard and mouse | Gamepad |
  |---|---|---|
  | Move | W A S D | left stick |
  | Run | Shift | left stick pressed in |
  | Look | mouse (only while it is held), arrow keys | right stick |
  | Jump | Space | South button |
  | Zoom | mouse wheel | d-pad up and down |

  - **Sticks:** a radial dead zone of 0.2, with the rest of the range rescaled to 0..1.
  - **Combined movement:** at most length 1.
  - **Pitch:** can be inverted.
- **`PlayerCharacter`:** owns a physics character and a `FollowCamera`. Each frame the caller calls:
  1. `steer(intent)`: the view turns and zooms at once.
  2. `before_step()` before each fixed step: it sets the character's movement and any waiting jump.
  3. `view()`: returns the camera for the frame.
- **Movement:** relative to where the camera faces. Walk 4 m/s, run 7 m/s, jump 5 m/s.
- **Jump buffer:** a jump waits up to 0.1 s for walkable ground. It is never left pending in the world.
- **The body:** faces the way it last moved.
- **Zoom:** 1 m a step, up to 8 m. Closer than one step is first person, so the camera never sits inside the head.
- **The course** (`samples/course/obstacle_course.hpp`), in order:
  1. rolling terrain on a height field;
  2. a 20° ramp to a 1.5 m platform, and a drop off its end;
  3. 0.3 m stairs;
  4. a 0.6 m ledge that needs a jump, and a 1.8 m drop;
  5. a 3 m wide tunnel with a 2.4 m ceiling;
  6. a 60° slope.
- **Sandbox `--play`** (with `--vulkan`): the course with a character, the crates falling beside it.
  - The character is drawn as a box with a visor, hidden in first person.
  - R starts again, as does falling off the world.

## Contracts and compatibility

- **New:**
  - `lykoware/player.hpp` and `engine/src/player.cpp`;
  - `samples/course/obstacle_course.hpp`;
  - the `player.course` test;
  - the sandbox's `--play`.
- **Changed, additions only:**
  - `MouseState` (two fields);
  - `Window` (two functions);
  - `FollowCamera::distance()`.
- **Unchanged:** existing behaviour. The sandbox without `--play` is as before.
- **Ownership:** a `PlayerCharacter` owns its character and destroys it with itself. The physics world must outlive it.
- **Repeatability:** a run repeats exactly in one build on one machine. It is not pinned across platforms: the steering uses `std::sin`, `std::cos` and `std::atan2`, whose last bit may differ between C libraries.
- **Not yet:**
  - smoothing the drawn character and camera between 60 Hz ticks;
  - an over-the-shoulder camera offset;
  - acceleration and turning curves (movement starts and stops at once);
  - coyote time;
  - crouch;
  - key rebinding;
  - skeletal animation, foot IK, ragdoll;
  - a recorded replay of a person's play.

## Consequences

- **Exit criteria covered:** a person can play Phase 2's terrain. Its "fixed-seed obstacle/slope/step/fall fixtures" and "no persistent ground penetration or camera-through-wall failures" have a headless test.
- **Owner's call:** whether the character *feels* right. The exit asks for owner-reviewed clips. The numbers above are starting points to tune in play.

## Verification

- **`player.course`** (new; physics and input only):
  - **Input mapping:**
    - diagonals are not faster;
    - the dead zone is cut out and rescaled;
    - mouse, stick and arrow-key turns go the right way, at the right rate;
    - a free cursor does not turn the view;
    - inverted pitch works;
    - Space and South jump once per press;
    - Shift runs and the wheel zooms.
  - **Movement:** walks where the camera faces at walking speed, runs to the camera's right at running speed, stops when let go, and faces its way.
  - **Jumps:**
    - one press jumps once;
    - a press between steps is not lost;
    - a press 3 ticks before landing jumps on landing; one 15 ticks before does not;
    - holding the button does not bunny-hop.
  - **Zoom:** steps of 1 m; first person past the last step; never between 0 and 1 m; stops at 8 m.
  - **Refusals:** a bad eye height, a negative speed and the world's own refusal create nothing. The character goes with the player.
  - **The course:** a fixed-seed bot plays it frame by frame through `read_player_intent`.
    - Frame times vary from 30 to 144 Hz; physics steps at 60 Hz.
    - It steers with the mouse, looks up and down at random, walks with keys or the stick, and jumps with Space.
    - Every checkpoint must be met: the platform heights, both landings, the ledge stopping it until it jumps, and the steep slope stopping it.
    - Every tick, the feet against the surface below: never sunk more than 2 cm for more than 2 ticks in a row. Measured: 1.3 mm deepest, never 2 ticks.
    - Every frame, the camera:
      - nothing between the pivot and the eye;
      - the eye never within the near plane of a block;
      - the eye at least 0.09 m above the terrain (measured 0.199 m);
      - the tunnel's ceiling pulls the camera in for most of it (95 of 98 frames).
    - The same seed repeats bit for bit. Another seed takes a different path and finishes too.
- **`input.mapping` and `window.lifecycle`:** motion adds up and lasts one frame, through the pump too; relative mode turns on and off, or names why the driver refuses it.
- **`sandbox.play`** (new): `--play` without `--vulkan` is refused. `--window --offscreen --vulkan --play --frames 60` runs on lavapipe, and the player stands at the start.
- **Rendered by hand:** the course was rendered by hand on lavapipe at each checkpoint and looked right.
- **Deliberate breaks:** 31 of 31 caught by `player.course`, `input.mapping` and `window.lifecycle`.
  - **Movement:**
    - not camera-relative;
    - right reversed;
    - running ignored;
    - facing not updated.
  - **Jumps:**
    - no buffer;
    - a buffer that never expires;
    - passed in the air;
    - never passed.
  - **Sticks:**
    - diagonals not limited;
    - the dead zone not rescaled;
    - no dead zone;
    - the stick's forward reversed.
  - **Looking:**
    - mouse-look ignoring its flag;
    - mouse yaw reversed;
    - inverted pitch ignored;
    - stick turning ignoring the frame length;
    - arrow keys reversed.
  - **Buttons:**
    - jump only on Space;
    - jump on a held Space.
  - **Zoom:**
    - sitting inside the head;
    - unbounded.
  - **The player:**
    - the pivot at the feet;
    - the character left behind;
    - an eye above the capsule accepted;
    - a negative speed accepted.
  - **Input:**
    - motion not cleared each frame;
    - motion replacing instead of adding;
    - non-finite motion kept;
    - the window dropping motion;
    - relative mode never set.
  - **The camera:** obstruction handling off.
- **Local results:** 59/59 pass in the Debug, ASan/UBSan and clang-tidy presets, with no skips. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion` sweep finds nothing in the new code.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
