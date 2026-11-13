# Assignment — Grok

You are Grok, working in `Avrenthrad/lykowareGE`. Joshua opens a chat with you. Stay in it across steps. This file is your queue. The shared map is [COORDINATION.md](COORDINATION.md). The roadmap is [docs/ROADMAP.md](../ROADMAP.md). The IDs are [docs/MASTER_CHECKLIST.md](../MASTER_CHECKLIST.md).

## How to use this file

1. Read this file, the coordination page, the roadmap, and the top of [docs/STATUS.md](../STATUS.md).
2. The rolling checklist is the queue below. Keep it in this file. Tick a box here after the checkpoint squash-merge, and only when that step was merged-ready: the branch builds, the new tests pass, and CI is green on Windows Debug, Windows Release, and the three Linux presets. A red or pending PR is noted, not ticked.
3. One checklist ID per branch and pull request. Stay in this chat across steps. Checkpoint and self-merge only as the Working rules say. Never merge another assistant's PR. Never force-push `main`.
4. A step marked **Joshua runs this** is not yours to tick. While it waits, start the next ID that is not blocked.
5. When an ID's done-condition is met, tick it on the master checklist in that same PR. Tick a rollup parent only when its note says it may close.

## Out of scope

The editor is Track E (master checklist section 8, roadmap stages E1–E5). It lives in `lykowareEditor` and is paused. The built-in content suite is Track C (section 9, stages C1–C5). Section 15 (Howler, Pax, other Lykoleon projects) has its own roadmaps. Do not start any of them. Combat, building, and faction work are engine systems with CLI fixtures. They are not an editor, a mesh suite, or a Pax client.

## Owned areas

- `tools/validation/` (the measurement harness and its tests)
- `tools/cli/` except `scene_command.*` (GPT), `character_command.*` and `animation_command.*` (Claude)
- `input.hpp`, `input.cpp`, `log.hpp`, `log.cpp`, `trace.hpp`, `trace.cpp`, `window.hpp`, `window.cpp`
- New modules you add: `combat`, `items`, `building`, `faction` (headers under `engine/include/lykoware/`, sources under `engine/src/`)
- Gameplay samples other than `samples/course/` (Claude owns the course). Prefer a new file over editing `samples/sandbox/main.cpp`; a few registration lines in `main.cpp` are allowed.
- Tests for the above

`05.04` (scaled navigation) is GPT's, inside the battle slice. You own the rest of section 5.

## Do not touch without coordination

- Claude's files: `rhi/`, `engine/shaders/`, `render`, `mesh`, `image*`, `gltf`, `skinning`, `animation*`, `foot_plant`, `ik`, `retarget`, `physics`, `physics_ecs`, `locomotion`, `player`, `npc`, `follow_camera`, `samples/course/`
- GPT's files: `scene`, `ecs`, `clock`, `audio*`, `script/`, `transform`, `tools/cli/scene_command.*`, and new `navigation`, `jobs`, `streaming`, `ui`, `save` modules
- `third_party/`
- Claude's and GPT's assignment files, and `COORDINATION.md`

Use public APIs. `NpcCharacter` and `LocomotionMotor` are Claude's and are already on `main`; call them, do not edit them. Camera refinement is Claude's `02.14` and `follow_camera.hpp`. Your combat camera is a new type. Physics joints exist (`02.12a`); mass and posed bodies (`02.12b`) are paused with Claude. Save migration format is GPT's `07.10`.

## What the other two are doing

**Claude** owns rendering, animation, physics, and the character motor: `02.17d` remainder (start, stop, and pivot clips), then ragdolls `02.12b`–`d` and stumble `02.13`, camera `02.14`, crowd profiling `02.11b-runtime-scale`, character gates `02.16`, then `07.03`–`07.05`, `06.10`, and `06.08`. Ragdolls stay paused until Claude finishes the `02.17d` remainder, unless Joshua reorders.

**GPT** owns navigation, jobs, assets, UI and text, audio completion, save and migration, streaming, replay, and the co-op boundary audit: `07.08`, `05.04`, `07.09`, `07.01`, `07.02`, `07.06`, `07.07`, `07.10`, `07.13`, `07.14`, and `06.01`–`06.07`, `06.09`, `06.11`, `06.12`. Later `14.05`–`14.07`.

## Dependencies that change your order

- `03.08` may use `NpcCharacter::seek` now. Navmesh behavior waits on GPT `07.08`.
- `03.03` does not edit `follow_camera.hpp`. If you need a shared hook, ask Claude to land it in `02.14`.
- `03.07` may use today's audio mixer. Voice priority waits on GPT `07.07` only if the scenario needs more voices than the mixer already plays.
- `04.06` full collapse waits on Claude `02.12b` when a piece needs mass or a posed body. A support graph can start earlier on today's bodies.
- `04.07`'s migration format waits on GPT `07.10` and `06.07`. A fixture may round-trip through today's scene commands before that.
- `05.03` and `05.08` wait on GPT `07.08` and `05.04`.
- `05.07`'s GPU side waits on Claude `07.04` if batching is missing. Simulation LOD does not.
- `01.12` waits on Joshua's runs of `01.09`–`01.11`.
- Sections 10–14 wait until GPT's `06.12` is ticked and Joshua opens the next phase. `10.08` and `14.12` are Joshua and Pax, not code you file.

## Working rules

- One branch and one PR per ID. Small PRs.
- Stay in this chat across steps. Do not checkpoint after every step. Checkpoint only when context is long (about 3–5 PRs) or you are losing track. Do not checkpoint before one PR is open, unless you are near the context limit.
- At each checkpoint, before "Checkpoint saved — start a new chat", squash-merge your own open PRs into `main` only when all five CI checks are green and the PR touches only your assigned items. Leave a red or pending PR open and note it. After the merge, tick the item and leave its one-line note. Never merge another assistant's PR. Never force-push `main`. If the merge conflicts, rebase on `main`, re-run CI, and merge only once green.
- Keep CI green. Add tests with each change, including one deliberate failure of the new behavior.
- Update the master checklist when an ID completes, in that PR. Do not tick a hardware or feel gate from Linux-only evidence.
- No new third-party dependency or paid service without Joshua's approval. The repo stays on GitHub. No model API, paid inference, or telemetry upload.
- Match the C++ style in `AGENTS.md`: namespace `lykoware`, four-space indent, braces on a new line for function bodies, self-contained headers.
- An interface change gets an ADR under `docs/adr/` before other work builds on it.
- The harness already covers inventory and offscreen timings (`01.09a`, `01.10a`, `01.11a`, [docs/LOCAL_VALIDATION.md](../LOCAL_VALIDATION.md)). Extend it. Do not replace it, and do not invent a measured 4070 result.
- Standard close for every ID:
  - [ ] Tests cover the new behavior and a deliberate break of it.
  - [ ] The PR's CI is green on all five hosted jobs.
  - [ ] The master checklist ticks this ID only, with the evidence line.

## Hardware steps

The PC is Joshua's: Windows 11, Intel Core i7-14700F, 16 GB RAM, owner-reported NVIDIA RTX 4070. VRAM and driver are unmeasured. You prepare the script and the instructions. **Joshua runs it.** Hosted Windows CI is not that run.

## Queue

### 1. `01.09` — confirm the GPU and VRAM

Tooling exists. The measurement does not.

- [ ] Confirm `measure-scene.ps1` still records adapter name, VRAM, and driver, and that a missing tool stays `incomplete` rather than a guessed 4070. Done when: a unit test covers a failed driver query, which `01.09a` already started; extend it only if a field is absent.
- [ ] Write the exact command and what Joshua should paste back, in the PR and in `docs/LOCAL_VALIDATION.md`. Done when: the instructions do not ask him to interpret a code path.
- [ ] **Joshua runs this** on the RTX 4070 PC. You do not tick this step.
- [ ] After he accepts the pasted inventory, standard close. Done when: `01.09` is ticked from that inventory, not from the owner report alone.

### 2. `01.10` — frame times, hitches, memory, GPU usage

- [ ] Extend the harness so one run records frame-time distribution, hitch count, process RAM, and whatever GPU time the existing trace API already exposes. Done when: a synthetic overrun fails, and a software GPU is refused as hardware evidence.
- [ ] Leave columns Claude and GPT can fill later (`02.11b-runtime-scale`, `02.16`, `06.11`) without you owning their fixtures. Done when: the script accepts a fixture path argument.
- [ ] **Joshua runs this.** You do not tick this step.
- [ ] After he accepts the run, standard close. Done when: `01.10` is ticked.

### 3. `01.11` — agree budgets

Proposed numbers live in [docs/PERFORMANCE.md](../PERFORMANCE.md). They are not agreed.

- [ ] Draft a budget sheet from that document and from Joshua's `01.09` and `01.10` numbers, once those runs exist. Done when: every number is labeled proposed or measured, and none is silently defaulted into CI.
- [ ] **Joshua agrees the budgets** (or changes them). You do not tick this step, and you do not agree them yourself.
- [ ] After he agrees, standard close. Done when: `01.11` is ticked and PERFORMANCE.md says the agreed values.

### 4. `07.12` — logging, crash reports, profiling, capture

Can proceed beside the harness. Do not claim a crash uploader. Local files only.

- [ ] A crash or fatal log written to a local path the test can read, and a capture command that fails clearly when the GPU is missing. Done when: the test sees the file, and nothing is uploaded.
- [ ] Standard close. Done when: `07.12` is ticked.

### 5. `07.11` — input rebinding and controllers

`input.hpp` already reads keyboard, mouse, and gamepad.

- [ ] Rebind an action in data, and load a binding set. Done when: a test rebinds, a bad binding is refused, and an absent gamepad does not fail the keyboard test.
- [ ] Standard close. Done when: `07.11` is ticked.

### 6. `07.15` — dependency, licence, and package audit

- [ ] A report of vendored packages against `third_party/README.md` and ADR 0000: name, version, licence. Done when: the test or script fails if a vendored directory has no notice, and the PR adds no dependency.
- [ ] Standard close. Done when: `07.15` is ticked.

### 7. Sandbox NPC — leftover, no checklist ID

`02.17e` is done (`NpcCharacter` on the motor, #85). STATUS noted that no NPC is in the sandbox. That was not part of the checklist item. One small PR, then return to the queue.

- [ ] Spawn one NPC on the existing course path using `NpcCharacter::seek`, without editing the motor. Done when: a headless sandbox check sees the NPC move and stop, and `02.17e` stays ticked.

### 8. `02.15` — character feel on Windows hardware

You write the script. Joshua watches and accepts. Claude owns the course code.

- [ ] Instructions that launch the existing course and direction fixture (`--directions`, `--agility`) and say what to look at (slide, pop, camera in a wall). Done when: the command is copy-pasteable and does not require a code change in `samples/course/`.
- [ ] **Joshua runs this** on the RTX 4070 PC and says whether the feel is accepted. You do not tick this step.
- [ ] After he accepts, standard close. Done when: `02.15` is ticked. In that PR, also tick `02.02a/02.03a-Windows` if the same run covered the hardware half of that substep, and tick rollups `02.02` and `02.03` only then. Hosted Windows CI is not enough, because the substep asks for the hardware check. Do not rebuild fitting or profiles.

### 9. `03.01` — a small combat scenario

- [ ] ADR or contract: two actors, one hit, one miss, one death, a restart, fixed seed. Done when: the PR states that results come from commands on the fixed step, not from wall-clock time.
- [ ] A headless fixture. Done when: the test fails if the event order changes.
- [ ] Standard close. Done when: `03.01` is ticked.

### 10. `03.02` — input buffering and actions

- [ ] A buffer that remembers an attack pressed slightly early, and an action state that refuses a second attack during the first. Done when: both cases are tested.
- [ ] Standard close. Done when: `03.02` is ticked.

### 11. `03.03` — targeting, aiming, combat cameras

- [ ] A target selection and a combat camera in a new type. Done when: the test locks a target, clears it, and does not edit `follow_camera.hpp`. If you cannot aim without a hook there, stop and name the hook for Claude's `02.14` instead of editing that file.
- [ ] Standard close. Done when: `03.03` is ticked.

### 12. `03.04` — attacks, blocking, dodging, interruption

- [ ] The four actions on the `03.02` state, with interruption rules in the contract. Done when: a test shows a dodge that cancels an attack, and a block that does not, or whatever the contract says, and the opposite case fails.
- [ ] Standard close. Done when: `03.04` is ticked.

### 13. `03.05` — hit detection and damage events

- [ ] A deterministic hit query and a damage event on the command stream. Done when: the same seed hits the same target, and a swung attack that misses is a tested miss.
- [ ] Standard close. Done when: `03.05` is ticked.

### 14. `03.06` — health, stamina, status

- [ ] Health and stamina change only through events, and a status expires on the fixed step. Done when: death, a stamina refusal, and expiry are tested.
- [ ] Standard close. Done when: `03.06` is ticked.

### 15. `03.07` — animation, hit windows, sound, VFX

Use animation events and sockets already on `main` (`02.09`). Do not edit the animation core.

- [ ] A hit window driven by an animation event, plus a sound call and a VFX marker the test can see. Done when: damage lands only inside the window, and removing the event stops the damage.
- [ ] Standard close. Done when: `03.07` is ticked.

### 16. `03.08` — enemy perception, navigation, decisions

- [ ] Perception and a decision that uses `NpcCharacter::seek` on a fixed route. Done when: the enemy closes, stops at the radius, and does not call a navmesh that does not exist yet.
- [ ] When GPT's `07.08` has merged, a follow-up PR switches the route to that query. Done when: the enemy goes around a block. If `07.08` is not merged, tick `03.08` for the seek path and write "navmesh follow-up open" on the checklist line instead of ticking it closed. Tick it closed only when both paths agreed in the contract are tested.

### 17. `03.09` — equipment and weapon attachments

Sockets exist. Attach through them.

- [ ] Equip and unequip a weapon on a socket, and refuse a missing socket. Done when: the test covers both, and the weapon's hit uses `03.05`.
- [ ] Standard close. Done when: `03.09` is ticked.

### 18. `03.10` — automate combat scenarios

- [ ] The `03.01` scenario runs unattended through CLI, including death and restart. Done when: one command reproduces the event list.
- [ ] Standard close. Done when: `03.10` is ticked.

### 19. `03.11` — responsiveness, readability, performance

- [ ] Automated checks for hit timing against the window. Done when: a late hit is rejected by test.
- [ ] **Joshua runs this** for feel and frame time, using the `01.10` harness. You do not tick this step.
- [ ] After he accepts, standard close. Done when: `03.11` is ticked.

### 20. `04.01` — items, resources, recipes

- [ ] Data for an item, a resource, and a recipe, refused when malformed. Done when: a test crafts the recipe on paper (inputs in, outputs out) with no world yet.
- [ ] Standard close. Done when: `04.01` is ticked.

### 21. `04.02` — inventory and crafting

- [ ] An inventory that crafts a `04.01` recipe and refuses a missing input. Done when: both cases are tested, and the inventory size is an explicit limit.
- [ ] Standard close. Done when: `04.02` is ticked.

### 22. `04.03` — placement, snapping, previews

- [ ] A placement preview that snaps to a grid the test sets, and that does not spawn a body until confirmed. Done when: the test sees the preview pose and an empty physics world before confirm.
- [ ] Standard close. Done when: `04.03` is ticked.

### 23. `04.04` — placement validation and collision

- [ ] Reject a placement that overlaps a solid. Done when: the overlap is refused and a clear placement is accepted, using the existing physics query.
- [ ] Standard close. Done when: `04.04` is ticked.

### 24. `04.05` — structural support and load

- [ ] A support graph: a piece is supported by the ground or by another piece, and a cycle or a floating piece is classified. Done when: the test covers ground, support, float, and cycle. This graph does not require `02.12b`.
- [ ] Standard close. Done when: `04.05` is ticked.

### 25. `04.06` — damage, destruction, collapse

- [ ] Remove a supporting piece and apply the `04.05` rule. Done when: dependents are marked collapsed in the test.
- [ ] If a collapsed piece must become a dynamic body with mass, wait for Claude `02.12b` and then attach it. Done when: the PR says whether it waited. Tick `04.06` only when the collapse the contract describes is tested, including the physics piece if the contract required mass.

### 26. `04.07` — persist construction

- [ ] Save and load a built fixture through the scene command path that exists today. Done when: a reload matches, and a bad file is refused.
- [ ] When GPT `07.10` merges, add the migration case if the version changed. Done when: `04.07` is ticked for the round trip. If the checklist sentence requires migration and `07.10` is open, leave `04.07` open and say so.

### 27. `04.08` — large structures and failures

- [ ] A structure at a count the test sets, plus a failed placement that does not leak a body. Done when: body count returns to the previous value after the failure.
- [ ] Standard close. Done when: `04.08` is ticked.

### 28. `04.09` — building performance gates

- [ ] The fixture and the columns (bodies, step time, memory). Done when: it runs headless and the PR points at the harness.
- [ ] **Joshua runs this** on the RTX 4070. You do not tick this step.
- [ ] After he accepts, standard close. Done when: `04.09` is ticked against the budgets he agreed in `01.11`. If `01.11` is still open, do not tick `04.09`.

### 29. `05.01` — selection and RTS camera

Can start before navigation. Do not edit `follow_camera.hpp`.

- [ ] Select units in a box and a top-down camera. Done when: the test selects, deselects, and keeps the third-person camera path unchanged when this camera is off.
- [ ] Standard close. Done when: `05.01` is ticked.

### 30. `05.02` — orders and command queues

- [ ] A queue of orders on the command stream. Done when: replay reproduces the queue, and a bad order is refused.
- [ ] Standard close. Done when: `05.02` is ticked.

### 31. `05.03` — group movement and formations

Blocked on GPT `07.08` and `05.04` for routing. The formation offsets can be tested before that.

- [ ] Offsets for a formation, applied as wanted velocities through `NpcCharacter`. Done when: the test holds the offsets on an open plane.
- [ ] After GPT's nav API merges, route the group around a building. Done when: the group does not walk through the solid. Tick `05.03` only when that route exists. If nav is not merged, leave the ID open.

### 32. `05.05` — faction, affiliation, hostility

Not the memory model in section 10. A team id and a hostility rule.

- [ ] Two factions, a hostility table, a test that a unit does not attack a friend. Done when: the table is data, and a missing faction is refused.
- [ ] Standard close. Done when: `05.05` is ticked.

### 33. `05.06` — tactical AI and objectives

- [ ] A seeded decision that pursues an objective (hold a point, or destroy a target) using `05.02` orders. Done when: the same seed gives the same orders, and a dead unit issues none.
- [ ] Standard close. Done when: `05.06` is ticked.

### 34. `05.07` — simulation, animation, and rendering detail

- [ ] A simulation LOD: far units update less often, deterministically. Done when: a test shows a far unit stepped less often and a near unit every step.
- [ ] Rendering LOD only by calling Claude's API if `07.04` has published one. Done when: the PR either uses that API or says the GPU half is waiting, and `05.07` is ticked only when both halves the checklist names are done.

### 35. `05.08` — larger battles

Blocked on `05.04`.

- [ ] Seeded battles at increasing counts, up to the proposed 256, that do not leak units after restart. Done when: the test checks count and restart. The frame budget is Joshua's (`05.09`, `01.11`), so this ID ticks on correctness, and the PR says the budget is the next ID.

### 36. `05.09` — unit-count budgets

- [ ] The fixture from `05.08` plus columns. Done when: the harness can run it.
- [ ] **Joshua runs this** and agrees a count. You do not tick this step.
- [ ] After he agrees, standard close. Done when: `05.09` is ticked.

### 37. `05.10` — direct-control and command-view transitions

- [ ] Switch between the character and the RTS camera without dropping the selection or the character's motor state. Done when: a test enters RTS, issues an order, returns, and finds the player where the motor left them.
- [ ] Standard close. Done when: `05.10` is ticked.

### 38. `01.12` — close Phase 1 acceptance

Do this only after Joshua's `01.09`, `01.10` and `01.11` steps are accepted. Phase 1's code (window, clocks, ECS, rendering, audio, commands, scripting, CLI capture) is largely on `main`. The open exit is evidence and budgets.

- [ ] A table in the PR: each Phase 1 exit line in the roadmap, the commit that covers it, or the ID still open. Done when: the table does not mark a hardware line done from Linux.
- [ ] Standard close. Done when: `01.12` is ticked only if that table shows no open Phase 1 exit. Otherwise leave it open and list the gaps.

### 39. Verification substep `02.02a/02.03a-Windows`

Not a section 1–7 feature. The fitting and profile code is merged. Hosted Windows CI already builds it. The open words are the hardware check.

- [ ] Fold the check into the `02.15` instructions (the imported character on the course). Done when: the instruction names the character profile command.
- [ ] **Joshua runs this** as part of `02.15`. You do not tick it yourself.
- [ ] Tick the substep in the `02.15` PR after he accepts. Done when: the substep is ticked and the note cites his run, not only hosted CI.

## Rollups you close

| ID | Tick when |
| --- | --- |
| `02.02`, `02.03` | `02.15` and `02.02a/02.03a-Windows` are ticked. No new fitting code. |

`02.04`, `02.05` and `02.16` are Claude's. Do not tick them.

## Later wave — do not start until `06.12` is ticked and Joshua says to continue

Sections 10–13 and the section 14 IDs below are yours so they are not unowned. They are not permission to start them during the core. `10.08` and `14.12` are legal and release gates: you record them, you do not file them, and you do not tick them.

### Section 10 — Living Memory

| ID | Done when |
| --- | --- |
| `10.01` | Persistent NPC ids and a life-goal record round-trip in a test, and the ID is ticked. |
| `10.02` | A relationship and a remembered event survive save/load. |
| `10.03` | A rumour reaches a second faction's knowledge and a test shows who knows it. |
| `10.04` | An alliance or a territory change is a command, replayed. |
| `10.05` | Unloaded-region simulation uses GPT's unloaded-region persist path and does not run a full physics world off-screen. Tick only when that is tested. |
| `10.06` | A CLI inspect command prints one NPC's memory. |
| `10.07` | A multi-step scenario reproduces the same consequences after reload. |
| `10.08` | The checklist line stays open. The PR may only point at the patent/Pax gate. Joshua and counsel do the review. You do not tick `10.08`. |

- [ ] Do these in order, one PR each, standard close, with the `10.08` exception.

### Section 11 — local world AI

No server, no paid API, no personal data leaving the machine (roadmap Phase 9). On-device only, and only after Joshua approves any model dependency.

| ID | Done when |
| --- | --- |
| `11.01` | **Joshua runs** a benchmark you prepared. You tick only after he accepts the numbers. |
| `11.02` | Inference budget is a number inside the `01.11` budgets, not a new silent cap. |
| `11.03` | Authored constraints reject an out-of-lore proposal in a test. |
| `11.04` | A knowledge query cannot see facts the actor has not been given. |
| `11.05` | A proposal is a struct the test can read, not free text applied to the world. |
| `11.06` | An illegal world action in a proposal is rejected and recorded. |
| `11.07` | Timeout, garbage, and a missing model all fall back to authored behavior. |
| `11.08` | A test shows no export of the fixture's personal-data stand-in. |
| `11.09` | A long scripted session recovers after a failed proposal. |
| `11.10` | **Joshua runs** the offline performance check. You tick only after that run. |

- [ ] One PR per ID. Stop and ask Joshua before adding a model file or a download.

### Section 12 — story

Depends on section 10's relationship records.

| ID | Done when |
| --- | --- |
| `12.01` | Quest, dialogue, and story-state formats have a schema and a refusal test. |
| `12.02` | A condition, a trigger, an objective, and a reward fire in a fixture. |
| `12.03` | A branch persists across save/load. |
| `12.04` | A story flag reads a section 10 relationship and does not write it except through that API. |
| `12.05` | A generated event cannot clear a protected story flag; the test tries. |
| `12.06` | CLI inspect and a failing assertion for a stuck quest. No editor graph. |
| `12.07` | A traversal of the fixture's branches, unattended. |
| `12.08` | An old save and a blocked quest both recover in tests. |

- [ ] One PR per ID, standard close. Do not build E4 graphs.

### Section 13 — flagship slice

Integration of systems, not Track C tools. `13.01`–`13.03` need Joshua's creative decisions before you produce content. Paid generation needs his approval first.

| ID | Done when |
| --- | --- |
| `13.01` | Joshua has written the scope you are allowed to implement. You do not invent it, and you do not tick this before he does. |
| `13.02` | Style notes he accepts are in the repo. |
| `13.03` | Original fixture content, with provenance, loads through the existing glTF path. No content-suite tool. |
| `13.04` | One slice runs combat, building, and strategy together under test. |
| `13.05` | That slice includes one section 12 consequence. |
| `13.06` | A command sequence produces a fixture without a paid service, unless he approved one. |
| `13.07` | **Joshua plays it.** You do not tick this. |
| `13.08` | Fixes from that playtest are merged and he accepts them. |
| `13.09` | A second person or a clean checkout can repeat the production commands; the PR shows the log. |

- [ ] One PR per ID, in order. Stop at `13.01` until Joshua locks the scope.

### Section 14 — production, your IDs

GPT owns `14.05`, `14.06`, and `14.07`. You own the rest. Still after `06.12`.

| ID | Done when |
| --- | --- |
| `14.01` | Supported OS and GPU list matches ADR 0021 and Joshua's `01.11` budgets. |
| `14.02` | A long run's memory is recorded. **Joshua runs** the multi-hour case on the PC; a short CI soak can land first but does not tick the ID alone. |
| `14.03` | **Joshua runs** or arranges AMD, Intel, and NVIDIA checks. You prepare the instructions and tick only after results exist. |
| `14.04` | A clean-machine install path is documented and tested as far as CI can, and Joshua confirms a clean Windows install. |
| `14.08` | Security and licence notes cite the `07.15` audit and add any new gap. No new dependency. |
| `14.09` | Public engine APIs you added have a short usage note next to their headers' existing style. |
| `14.10` | Versioning and regression policy is a doc Joshua accepts, with the CI list. |
| `14.11` | A starter template builds a new project through documented commands, on CI. |
| `14.12` | You do not tick this. Owner acceptance and Pax review are Joshua's. |

- [ ] One PR per ID. Leave `14.12` open.
