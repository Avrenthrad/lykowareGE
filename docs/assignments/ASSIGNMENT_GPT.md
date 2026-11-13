# Assignment — GPT

You are GPT (Codex), working in `Avrenthrad/lykowareGE`. Joshua opens a fresh chat with you. This file is your queue. The shared map is [COORDINATION.md](COORDINATION.md). The roadmap is [docs/ROADMAP.md](../ROADMAP.md). The IDs are [docs/MASTER_CHECKLIST.md](../MASTER_CHECKLIST.md).

## How to use this file

1. Read this file, the coordination page, the roadmap, and the top of [docs/STATUS.md](../STATUS.md).
2. The rolling checklist is the queue below. Keep it in this file. Tick a box here only when that step is merged-ready: the branch builds, the new tests pass, and CI is green on Windows Debug, Windows Release, and the three Linux presets.
3. One checklist ID per branch and pull request. Joshua merges. You never merge, and you never push to `main`.
4. A step marked **Joshua runs this** is not yours to tick.
5. While a step is waiting on Joshua, Claude, or Grok, take the next ID that is not blocked. Do not idle, and do not tick the waiting step.
6. When an ID's done-condition is met, tick it on the master checklist in that same PR.

## Out of scope

The editor is Track E (master checklist section 8, roadmap stages E1–E5). It lives in `lykowareEditor` and is paused. The built-in content suite is Track C (section 9, stages C1–C5). Section 15 (Howler, Pax, other Lykoleon projects) has its own roadmaps. Do not start any of them. Streaming, save, and UI work stay headless and command-driven. Do not build an editor shell, a viewport, or content-suite authoring tools.

## Owned areas

You own broad runtime systems. Existing files:

- `scene.hpp`, `scene.cpp`, `scene_script.hpp`, `scene_script.cpp`
- `ecs.hpp`, `ecs.cpp`
- `clock.hpp`, `clock.cpp`
- `audio.hpp`, `audio.cpp`, `audio_output.*`, `audio_spatial.*`
- `script/` and `script.hpp`
- `transform.hpp`
- `tools/cli/scene_command.*`
- Tests: `ecs*`, `scene_*`, `audio*`, `script*`, `clock*`
- `samples/scenes/` for streaming and save fixtures

New modules, headers under `engine/include/lykoware/` and sources under `engine/src/`, named for the system: `navigation`, `jobs`, `streaming`, `ui`, `save`. Each new public header states ownership, lifetime, threading, and failure.

## Do not touch without coordination

- Claude's files: `rhi/`, `engine/shaders/`, `render`, `mesh`, `image*`, `gltf`, `skinning`, `animation*`, `foot_plant`, `ik`, `retarget`, `physics`, `physics_ecs`, `locomotion`, `player`, `npc`, `follow_camera`, `samples/course/`
- Grok's files: `input`, `log`, `trace`, `window`, `tools/validation/`, `tools/cli/` other than `scene_command.*`, and new `combat`, `building`, `items`, or `faction` modules
- `samples/sandbox/main.cpp` except a few lines that call a new file you added
- `third_party/`
- Claude's and Grok's assignment files, and `COORDINATION.md`

Scene commands are yours. A gameplay PR may add a command only by calling an API you published. If they need a new command shape, you add it, or you review the hunk. Physics unload uses today's body create and destroy. If that is not enough, ask Claude for an API. Do not edit `physics.cpp`.

## What the other two are doing

**Claude** owns rendering, animation, physics, and the character motor. Queue: the `02.17d` clip remainder, then ragdolls `02.12b`–`d` and `02.13` (paused until that remainder, unless Joshua reorders), camera transitions `02.14`, crowd measurement `02.11b-runtime-scale`, character gates `02.16`, then lighting and materials `07.03`, batching `07.04`, environment drawing `07.05`, shader hitches `06.10`, and distant drawing `06.08` once your chunk API exists. Directories listed under "Do not touch".

**Grok** owns gameplay and the measurement harness: hardware prep `01.09`–`01.12` and `02.15`, combat `03.*`, building `04.*`, battle gameplay except `05.04`, input `07.11`, diagnostics `07.12`, licence audit `07.15`. Grok owns `tools/validation/`. You do not edit that directory. You describe the fixture and the columns; Grok adds the script hook.

## Dependencies that change your order

- `05.04` waits on your `07.08`.
- `06.11`'s PC run waits on Grok's harness and Joshua. The fixture does not.
- `06.12` is last. It waits on every Phase 0–6 owner and on Joshua's hardware gates.
- `06.08` is Claude's, and it waits on your `06.01` and `06.04`. Publish a small chunk query before you polish streaming.
- Grok's enemies (`03.08`) use `NpcCharacter::seek` until `07.08` exists, then switch to your nav API.
- Grok's formations and large battles (`05.03`, `05.08`) wait on `07.08` and `05.04`.
- Grok's save migration for buildings (`04.07`) waits on your `07.10` and `06.07`.
- Later IDs `14.05`–`14.07` wait until `06.12` is ticked and Joshua opens the next phase.

## Working rules

- One branch and one PR per ID. Small PRs.
- Joshua merges every PR. You never merge.
- Keep CI green. Add tests with each change, including one deliberate failure of the new behavior.
- Update the master checklist when an ID completes, in that PR. Do not tick hardware or Phase 6 acceptance from a Linux-only run.
- No new third-party dependency or paid service without Joshua's approval. The repo stays on GitHub. No network stack, sockets, or replication under `07.14` unless Joshua commits co-op to the release. ADR 0021 says single-player first, co-op-ready through commands.
- Match the C++ style in `AGENTS.md`: namespace `lykoware`, four-space indent, braces on a new line for function bodies, self-contained headers.
- An interface change gets an ADR under `docs/adr/` before other work builds on it.
- Register new tests by adding lines to the existing CMake test list. Do not rewrite another target's lines.
- Standard close for every ID:
  - [ ] Tests cover the new behavior and a deliberate break of it.
  - [ ] The PR's CI is green on all five hosted jobs.
  - [ ] The master checklist ticks this ID only, with the evidence line.

## Queue

### 1. `07.14` — co-op boundary, not networking

ADR 0021: the simulation is deterministic and driven by per-tick commands so 2–4 player co-op can be added later. No MMO, no persistent servers, no paid servers. Networking is not committed.

- [ ] A test that two command streams, concatenated, replay to the same state as one stream with the same commands. Done when: the test fails if a system reads the wall clock or an unseeded random source for a gameplay result it covers.
  - [PR #88](https://github.com/Avrenthrad/lykowareGE/pull/88), `gpt/07.14-coop-command-boundary`; concatenation and mutated-stream tests added; all five hosted checks pending. Scope: existing scene box commands only.
- [ ] A short note in the PR of what is deliberately absent (sockets, replication, matchmaking). Done when: the note cites ADR 0021 and adds no dependency.
- [ ] Standard close. Done when: `07.14` is ticked with the words that networking was not implemented because it is not committed.

### 2. `07.13` — replay and determinism where required

Scene replay exists (ADR 0031). Extend it where a gap would let gameplay diverge. Do not claim every future system is covered.

- [ ] Name the systems this PR puts under replay (the ones on `main` today that still change state off the command stream). Done when: the PR lists them, and each listed system has a 30/60/144 Hz equality test.
- [ ] Standard close. Done when: `07.13` is ticked for the systems you listed. A later system adds its own replay test in its own PR rather than reopening this ID without a checklist note.

### 3. `07.08` — generate navigation and update it

Grok's combat can ship without this, using `seek`. Your API is what they switch to.

- [ ] ADR: a nav representation, a query (from, to, agent radius), and an update when a solid is added or removed. Done when: the ADR says the query does not write physics, what an unreachable goal returns, and that results are deterministic for the same solids.
- [ ] A fixture: a route around a block, a blocked route, and a route that changes after a solid is added. Done when: all three are tested, and a deliberate wrong shortcut fails the test.
- [ ] Standard close. Done when: `07.08` is ticked. Tell Grok the header and the query signature in the PR.

### 4. `05.04` — scale navigation and avoidance

Blocked on `07.08`. This is the crowd half of the battle slice. The rest of section 5 is Grok's.

- [ ] Avoidance for many agents on the nav API, with a fixed seed. Done when: a test with overlapping goals does not let agents occupy the same point, and the result repeats bit for bit in one build.
- [ ] A count toward the proposed 256 agents (roadmap Phase 5). The timing budget is not agreed until Joshua's `01.11`. Done when: the test runs the count and records time, and the checklist note says the budget tick waits on `01.11` and `05.09` (Grok). Tick `05.04` when avoidance and the count fixture pass, and state that the budget number is still open under those IDs.

### 5. `07.09` — jobs, threading, scheduling, budgets

- [ ] ADR: a job queue, who may schedule, what runs on the main thread (physics and the command stream stay single-threaded as they are today), and how a job failure is reported. Done when: the ADR forbids a job from writing ECS or physics without a command.
- [ ] A test that runs N jobs, shuts down without a leak, and refuses a job that outlives its owner. Done when: the shutdown test fails if a job is detached and abandoned.
- [ ] Standard close. Done when: `07.09` is ticked. Resource budgets that need the RTX 4070 stay under Grok's `01.11`; your tests use explicit limits, not a silent default.

### 6. `07.01` — asset identifiers, dependencies, cooked packages

- [ ] ADR: an asset ID, a dependency edge, and a cooked package the runtime can open without the source tree. Done when: the ADR says IDs are stable strings, what a missing dependency does, and that the cook is a CLI command.
- [ ] Cook a small fixture and load it. Done when: changing a dependency without recooking fails the load, and an unchanged cook is byte-stable for the fixture.
- [ ] Standard close. Done when: `07.01` is ticked.

### 7. `07.02` — import diagnostics and incremental rebuilds

Uses `07.01`. The character inspect command stays Claude's. Your diagnostics are for the package graph.

- [ ] A CLI report of why an import failed, and a rebuild that skips an unchanged input. Done when: a test feeds a bad input and sees the diagnostic, and a second run does not rewrite an unchanged package.
- [ ] Standard close. Done when: `07.02` is ticked.

### 8. `07.07` — audio mixing, spatialisation, streaming, prioritisation

Mixer, output, and spatial audio exist (ADRs 0032–0034). This ID is what is still missing: streaming and prioritisation, and any gap those ADRs name as unfinished. Read them and list the gap in the PR before coding.

- [ ] Prioritise voices when there are more than the mix can play, and stream a long clip without loading it all at once. Done when: a test shows a nearer voice kept over a farther one, and a stream that is cancelled releases its buffer.
- [ ] Standard close. Done when: `07.07` is ticked. Grok's combat audio (`03.07`) may call the existing mixer before this; they switch to priorities when this merges.

### 9. `07.06` — UI, text, fonts, localisation, accessibility foundations

Headless. No editor. A UI tree the runtime can draw and that tests can query.

- [ ] ADR: text layout, a font you are allowed to vendor only if Joshua approves the dependency, localisation lookup, and one accessibility hook (a text scale or a reduced-motion flag). Done when: the ADR names the failure when a string is missing. If a font dependency is required, stop and ask Joshua before adding it.
- [ ] A fixture that lays out a string, swaps a locale, and reads the accessibility flag. Done when: a missing key is reported and does not crash.
- [ ] Standard close. Done when: `07.06` is ticked.

### 10. `06.01` — coordinates, regions, partitioning

World scale is 16 km or more, double precision (ADR 0021, ADR 0022).

- [ ] ADR: region coordinates, how a point maps to a region, and the stable ID of a region. Done when: the ADR uses double positions and states what happens at a boundary.
- [ ] Tests at the origin and past 8 km. Done when: the same point always maps to the same region, including on the boundary, which is defined one way.
- [ ] Standard close. Done when: `06.01` is ticked. Claude needs this before `06.08`.

### 11. `06.02` — asynchronous loading and cancellation

Uses your jobs (`07.09`) if that has merged. If it has not, a single background load with an explicit cancel is enough, and the PR says jobs will adopt it.

- [ ] Load a region fixture off the main thread and cancel it. Done when: a cancel before completion leaves no resident asset, and a completed load is the same bytes as a sync load.
- [ ] Standard close. Done when: `06.02` is ticked.

### 12. `06.03` — residency and eviction

- [ ] Track what a region holds and evict the least recently used when over a limit the test sets. Done when: exceeding the limit evicts, and a pinned asset is not evicted. No silent default budget. The number Joshua agrees in `01.11` is applied later, not invented here.
- [ ] Standard close. Done when: `06.03` is ticked.

### 13. `06.04` — stream terrain and world chunks

The chunk is data. Claude draws it (`07.05`, `06.08`). You do not write shaders.

- [ ] Stream a chunk in and out by the `06.01` region of a moving point. Done when: a test walks across a boundary, the new chunk is resident, the old one is evicted under the `06.03` rule, and collision for the chunk uses existing physics create/destroy.
- [ ] Publish the query Claude's `06.08` will call (chunk id, pose, a coarse and a fine payload). Done when: the header comment states lifetime: the streamer owns the bytes, the renderer borrows them for a frame.
- [ ] Standard close. Done when: `06.04` is ticked.

### 14. `06.05` — stream collision, navigation, audio, and entities

- [ ] A chunk brings collision, a nav update (`07.08`), an audio bank handle, and entities, and takes them away on eviction. Done when: after eviction, a query does not hit the unloaded collision, and a replay across the boundary matches a second run.
- [ ] Standard close. Done when: `06.05` is ticked.

### 15. `06.06` — persist changes in unloaded regions

- [ ] An edit in a region survives eviction and reload. Done when: a test edits, unloads, loads, and sees the edit, and a region that was never edited reloads as authored.
- [ ] Standard close. Done when: `06.06` is ticked.

### 16. `06.07` — version world and save data

- [ ] A version field on the save. An unknown newer version is refused. Done when: a test with a future version fails cleanly, and the current version round-trips.
- [ ] Standard close. Done when: `06.07` is ticked. Grok's `04.07` may depend on this.

### 17. `07.10` — save migrations, recovery, corruption

- [ ] Migrate one older fixture version forward. A truncated file is refused and does not overwrite the last good save. Done when: both cases are tested.
- [ ] Standard close. Done when: `07.10` is ticked.

### 18. `06.09` — rapid travel, teleports, memory pressure

- [ ] Teleport across many regions and cross the same boundary repeatedly. Done when: resident memory returns under the test's limit after the run, and no collision from an evicted region remains.
- [ ] Standard close. Done when: `06.09` is ticked.

### 19. `06.11` — throughput and frame pacing

- [ ] A streaming traversal fixture and the columns to record (load time, hitch, resident bytes). Done when: the fixture runs headless and the PR tells Grok the arguments. You do not edit `tools/validation/`.
- [ ] **Joshua runs this** on the RTX 4070 Windows PC. You do not tick this step.
- [ ] After Joshua accepts the run, standard close. Done when: `06.11` is ticked. A Linux timing is not the tick.

### 20. `06.12` — Phase 0–6 engine-core acceptance

Last. Do not start this while any Phase 0–6 ID owned by you, Claude, or Grok is open, or while Joshua's hardware steps for `01.09`–`01.12`, `02.15` and `02.16` are unticked.

- [ ] A record in the PR: each Phase 0–6 exit line from the roadmap, the checklist ID that covers it, and the commit or the statement that it is still open. Done when: every open line is still open on the checklist, and every closed line cites evidence.
- [ ] Standard close. Done when: `06.12` is ticked only if that record shows no open Phase 0–6 ID. If any remain, do not tick `06.12`; list them.

## Later wave — do not start until `06.12` is ticked and Joshua says to continue

### `14.05` — accessibility, localisation, settings

Extends `07.06`. Done when: a settings fixture persists the text scale and locale, a test covers a missing locale, and `14.05` is ticked after the standard close.

- [ ] Persist the `07.06` accessibility and locale settings through the save path.
- [ ] Standard close.

### `14.06` — save migration policy

Extends `07.10` and `06.07`. Done when: the policy (which versions the runtime still reads) is written in the PR and tested with one rejected version, and `14.06` is ticked.

- [ ] Write the policy and the test.
- [ ] Standard close.

### `14.07` — harden scripting, imports, and file handling

Extends `script/` and `07.02`. Done when: a hostile path and an oversized script are refused, the tests cover both, and `14.07` is ticked. No new scripting language.

- [ ] Refusal tests for the path and the script.
- [ ] Standard close.
