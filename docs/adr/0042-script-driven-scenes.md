# ADR 0042 — Scripts drive scenes through recorded commands

Status: Proposed.
Related task/PR: scripting PR 2 of 2 (branch `claude/script-commands`), stacked on scripting PR 1 ([ADR 0041](0041-lua-scripting.md)).
Supersedes / superseded by: none. Builds on scenes and commands ([ADR 0031](0031-scenes-and-command-replay.md)) and the Lua sandbox (ADR 0041).

## Context

Phase 1 asks for "a command dispatcher and scripting entry point".

- **The dispatcher exists:** versioned scene commands, replayed tick by tick (ADR 0031).
- **The sandbox exists:** a Lua state that is sandboxed and repeatable (ADR 0041).
- **What was missing:** a way for a script to act on a scene, and a way to run one from the command line.

Replays are central to this engine: the same commands must give the same scene, bit for bit. A script must not weaken that.

## Options

- **How scripts change a scene:**
  - **through the scene's own commands**, dispatched and recorded;
  - **a separate scripting API** that edits the ECS world or physics directly.

  Commands were chosen. A script then has no power a command file lacks. Its run can be written out as an ordinary command file and replayed without the script, which keeps ADR 0031's replay guarantee.
- **Script interface:**
  - **Lua tables** (`spawn{id = ..., position = {x, y, z}}`) through a small Lua prelude over plain-valued host functions;
  - **positional arguments only;**
  - **tables in `script::Value`.**

  The prelude was chosen. Scripts read naturally, and the PR 1 boundary stays plain values. The prelude raises errors at the script's own line.
- **Entry point:**
  - **`lyko run --scene ... --script FILE`**, the agent path ADR 0031 already uses;
  - **a sandbox window option.**

  The CLI was chosen. The sandbox's crates are not a `scene::Scene`.

## Decision

Proposed:

- **`lykoware/scene_script.hpp`:** `scene::ScriptDriver` runs a script against a `Scene`.
  - The script's top level runs once.
  - `run_tick()` calls its `on_tick(tick)` each tick, before the caller advances the scene. If the script defines none, it does nothing.
  - In Lua:
    - `spawn{id, position, half_extents, colour, motion}`: all but `id` optional, with the scene format's defaults;
    - `destroy(id)`;
    - `set_velocity(id, {x, y, z})`;
    - `position(id)`: returns `x, y, z`, or nil for an unknown or destroyed entity.
  - **Commands:**
    - each change becomes a `scene::Command` at the current tick, with sequence numbers rising from 1 across the run;
    - it is dispatched at once and recorded in `commands()`.
  - **Refusals:** a command the scene refuses raises a Lua error at the script's line, which `pcall` can catch, and changes nothing.
  - **Failures:** commands issued before a failure stay applied and recorded, as with `Scene::dispatch`.
  - **Hidden host functions:** the host functions behind the prelude are hidden from scripts.
- **`Scene::position(id)`:** the position of a live entity by its authored ID. This is the one addition to ADR 0031's API. It is read-only and used by `position()`.
- **`lyko run --scene FILE --script FILE [--record FILE] [--ticks N] ...`:** runs the script each tick.
  - `--record` writes its commands as a `lykoware.commands` file.
  - `--commands` and `--script` together are refused, as is `--record` without `--script`.
  - A failing script exits 2, with the script's file and line.
  - The report's `commands_applied` counts the script's commands.
- **Sample:** `samples/scenes/falling-crates.lua` issues the same three commands as `falling-crates.commands.json`.

## Contracts and compatibility

- **New:**
  - `lykoware/scene_script.hpp`;
  - `Scene::position`;
  - `lyko run --script` and `--record`;
  - the sample script.
- **Unchanged:** the scene and command formats, and existing CLI options.
- **Determinism:**
  - **Same build:** a script run gives the same scene every time.
  - **Recordings:** its recording replays to the same scene without the script, on any build that replays command files identically.
  - **Not replayable as a script:** a script that reads `math.random` is still repeatable (seeded by the host, ADR 0041), but only its recording replays across builds.
- **Not yet:**
  - scripts reacting to anything but positions (velocities, contacts);
  - scripts in the interactive sandbox window;
  - more than one script per scene;
  - script settings on the CLI (seed, limits). The CLI uses ADR 0041's defaults and seed 0.

## Consequences

- The Phase 1 deliverable "command dispatcher and scripting entry point" has both halves. An agent can write a Lua file and run it against a scene through `lyko`, then keep the recording as a replay.
- Whether the deliverable counts as done is the owner's call.

## Verification

- **`scene.script`** (new):
  - **The sample:** the falling-crates script gives the command file's scene at every one of 120 ticks. It records exactly the file's commands (identical canonical encoding), its recording replays to the same scene, and a second run is identical.
  - **Reacting:** a script that bounces green whenever it falls below 1 m reads positions, issues several velocity commands numbered 1, 2, 3… in tick order, and replays from its recording.
  - **Refusals** are catchable with `pcall` and change nothing. Each names its cause:
    - an unknown ID;
    - a used ID;
    - an unknown motion;
    - a non-table velocity;
    - a static body's velocity;
    - a non-table spawn.
  - **A failing `on_tick`:**
    - reports its file and line;
    - a command issued before the failure stays applied and recorded;
    - a refused command at the top level is reported at the script's line, not inside the prelude.
  - **Other checks:**
    - an endless `on_tick` is stopped by the instruction limit;
    - a script without `on_tick` is fine;
    - a syntax error fails `create` with its line;
    - a destroyed entity has no position;
    - the host functions are hidden.
- **`cli.integration`:**
  - `lyko run --script` gives the command file's state hash, applies 3 commands, and records the same ticks, sequences and types;
  - the recording replays to the same hash;
  - a failing script exits 2 naming `failing script.lua:2:` (a path with a space).
- **`cli.commands`:**
  - `--commands` with `--script` is refused;
  - `--record` without `--script` is refused;
  - a missing script file is refused.
- **Deliberate breaks:** 15 of 15 caught:
  - the sequence never rising;
  - commands not recorded;
  - refused commands recorded;
  - colour read from the extents;
  - velocity y dropped;
  - `on_tick` given the next tick;
  - the position of nothing being the origin;
  - host failures raised in the prelude;
  - the raw host functions left visible;
  - motion ignored;
  - a destroyed entity keeping a position;
  - the CLI never running the script;
  - the CLI recording nothing;
  - the CLI allowing commands and a script together;
  - the CLI recording without a script.
- **Local results:** to be recorded.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
