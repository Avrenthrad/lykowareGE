# Assignment — Claude

You are Claude, working in `Avrenthrad/lykowareGE`. Joshua opens a chat with you. Stay in it across steps. This file is your queue. The shared map is [COORDINATION.md](COORDINATION.md). The roadmap is [docs/ROADMAP.md](../ROADMAP.md). The IDs are [docs/MASTER_CHECKLIST.md](../MASTER_CHECKLIST.md).

## Rolling checklist

Tick only after the PR is merged with all five CI checks green. One line of notes per item.

- [x] `02.17d` remainder (clips + imported speeds) — #89 merged `7e67a06`; `02.17` parent ticked.
- [ ] `02.11b-runtime-scale` — fixture merged #92 `74009df`; waiting: **Joshua runs this** (`lykoware_ik_runtime_scale --characters 500 --frames 600 --csv ik_scale.csv`, Release).
- [x] `02.14` camera transitions — merged #96 (89dc40e)
- [ ] `07.03` lighting/shadows/materials/transparency/post (lettered slices)
- [ ] `07.04` visibility, batching, GPU resources
- [ ] `07.05` environment rendering
- [ ] `06.10` pipeline warmup (world-scale half: **Joshua runs this**)
- [ ] `02.12b` → `02.12c` → `02.12d` → `02.13` ragdolls — unpaused (02.17d merged); next
- [ ] `06.08` — blocked on GPT `06.01`, `06.04`
- [ ] `02.16` gates (perf run: **Joshua runs this**)

## How to use this file

1. Read this file, the coordination page, the roadmap, and the top of [docs/STATUS.md](../STATUS.md).
2. The rolling checklist is the queue below. Keep it in this file. Tick a box here after the checkpoint squash-merge, and only when that step was merged-ready: the branch builds, the new tests pass, and CI is green on Windows Debug, Windows Release, and the three Linux presets. A red or pending PR is noted, not ticked.
3. One checklist ID per branch and pull request. Stay in this chat across steps. Checkpoint and self-merge only as the Working rules say. Never merge another assistant's PR. Never force-push `main`.
4. A step marked **Joshua runs this** is not yours to tick.
5. While a step is waiting on Joshua or on GPT, take the next ID that is not blocked. Do not idle, and do not tick the waiting step.
6. When an ID's done-condition is met, tick it on the master checklist in that same PR. Tick a rollup parent only when its note below says it may close.

## Out of scope

The editor is Track E (master checklist section 8, roadmap stages E1–E5). It lives in `lykowareEditor` and is paused. The built-in content suite is Track C (section 9, stages C1–C5). Section 15 (Howler, Pax, other Lykoleon projects) has its own roadmaps. Do not start any of them. Do not build editor UI, mesh authoring, sculpting, or content-suite tools while doing a rendering or animation task.

## Owned areas

You own the rendering, animation, physics, and character-motor code:

- `engine/include/lykoware/rhi/`, `engine/src/rhi/`, `engine/shaders/`
- Headers and sources: `render`, `mesh`, `image`, `image_decode`, `gltf`, `skinning`, `animation`, `animation_graph`, `foot_plant`, `ik`, `retarget`, `physics`, `physics_ecs`, `locomotion`, `player`, `npc`, `follow_camera`
- `engine/src/image/`
- Matching tests: `rhi_*`, `render_*`, `image_*`, `gltf*`, `skinning`, `animation*`, `ik`, `foot_*`, `balance_shift`, `prepared_ik`, `physics_*`, `player_*`, `npc_locomotion`, `locomotion*`, `follow_camera`, `joint_axis`, `terrain_stance`, `rig_acceptance`, `single_support`, `drawn_support`, `pose_crossfade`, `authored_animation`, `mannequin`
- `samples/course/` for character and animation fixtures
- `tools/cli/character_command.*` and `tools/cli/animation_command.*` when an ID you own needs them

New ragdoll types stay behind `physics.hpp` and a new `ragdoll` header you add. No Jolt type in a public header.

## Do not touch without coordination

- GPT's files: `scene`, `scene_script`, `ecs`, `clock`, `audio*`, `script/`, and new `navigation`, `jobs`, `streaming`, `ui`, or `save` modules
- Grok's files: `input`, `log`, `trace`, `window`, `tools/validation/`, `tools/cli/` other than the two command files above, and new `combat`, `building`, `items`, or `faction` modules
- `samples/sandbox/main.cpp` except a few lines that call a new file you added
- `third_party/`
- GPT's and Grok's assignment files, and `COORDINATION.md`

If streaming or combat needs a new physics or camera call, add the smallest API in your module and say so in the PR. Do not take over their feature.

## What the other two are doing

**GPT** owns runtime systems and streaming: navigation (`07.08`, `05.04`), jobs (`07.09`), assets and import (`07.01`, `07.02`), UI and text (`07.06`), audio (`07.07`), save and migration (`07.10`, `06.06`, `06.07`), world streaming (`06.01`–`06.07`, `06.09`, `06.11`, `06.12`), replay (`07.13`) and the co-op boundary audit (`07.14`). Directories: `scene`, `ecs`, `audio`, `script`, and the new modules named above. Later, `14.05`–`14.07`.

**Grok** owns gameplay and the measurement harness: hardware prep (`01.09`–`01.12`, `02.15`), combat (`03.01`–`03.11`), building and crafting (`04.01`–`04.09`), battle gameplay except scaled navigation (`05.01`–`05.03`, `05.05`–`05.10`), input (`07.11`), diagnostics (`07.12`) and the licence audit (`07.15`). Directories: `tools/validation/`, most of `tools/cli/`, `input`, `log`, `trace`, and new combat, building, and faction modules. Later, sections 10–13 and the rest of section 14.

## Dependencies that change your order

- `02.12b` waits on your `02.17d` remainder, unless Joshua says to start ragdolls sooner. See the pause below.
- `02.13` waits on `02.12d`.
- `06.08` waits on GPT `06.01` and `06.04`. Start it when those APIs exist; do other rendering work first.
- The run half of `02.11b-runtime-scale` and of `02.16` waits on Grok's harness and on Joshua's PC. Write the fixture before that, and do not tick the run.
- Grok's combat cameras (`03.03`) must not rewrite `follow_camera.hpp`. If they need a shared hook, land it in `02.14` and name it in the PR.
- Grok's collapse (`04.06`) may ask you for mass and posed bodies from `02.12b`. That is the pause order, not a reason to start `02.12b` early.

## Working rules

- One branch and one PR per ID. Small PRs.
- Stay in this chat across steps. Do not checkpoint after every step. Checkpoint only when context is long (about 3–5 PRs) or you are losing track. Do not checkpoint before one PR is open, unless you are near the context limit.
- At each checkpoint, before "Checkpoint saved — start a new chat", squash-merge your own open PRs into `main` only when all five CI checks are green and the PR touches only your assigned items. Leave a red or pending PR open and note it. After the merge, tick the item and leave its one-line note. Never merge another assistant's PR. Never force-push `main`. If the merge conflicts, rebase on `main`, re-run CI, and merge only once green.
- Keep CI green. Add tests with each change, including one deliberate failure of the new behavior.
- Update the master checklist when an ID completes, in that PR. Do not tick Windows, hardware, or owner-feel from Linux-only evidence.
- No new third-party dependency or paid service without Joshua's approval. The repo stays on GitHub.
- Match the C++ style in `AGENTS.md`: namespace `lykoware`, four-space indent, braces on a new line for function bodies, self-contained headers.
- An interface change gets an ADR under `docs/adr/` before other work builds on it.
- Standard close for every ID, as its own steps:
  - [ ] Tests cover the new behavior and a deliberate break of it.
  - [ ] The PR's CI is green on all five hosted jobs.
  - [ ] The master checklist ticks this ID only, with the evidence line.

## Pause — ragdolls 02.12b–d

On 12 November 2026 Joshua ordered locomotion feel (ADR 0074, five PRs) ahead of ragdolls. STATUS and the work board record the pause: **02.12b–d wait until that work is done, or until Joshua reorders.** #82–#86 are merged. What remains of it is `02.17d`'s clips and imported speeds. Do `02.17d` first. Then start `02.12b`. Joints (`02.12a`, ADR 0073, #81) are done; do not rebuild them. Motors belong to `02.13`, not to `02.12`.

## Already done inside your area

Do not rebuild these. `02.11a` through `02.11d` (solvers, plants, heading, drawn supports, pelvis, rig acceptance, balance) are merged. `02.12a` joints are merged. `02.17a`–`c` and `02.17e` are merged. `02.17d` stride matching, lean, gait blend, pivot hold and the profile key are merged in #86 (`bb30510`, run 43339478172).

## Queue

Tick the boxes in this file. Order is the order you start work, skipping anything whose dependency is still open.

### 1. `02.17d` — remainder only

Stride matching, lean, gait blend, pivot hold and the profile `locomotion` key are merged. Left: authored start, stop and pivot clips, and imported characters' authored speeds. ADR 0074 already allows a fallback blend when a clip is absent, and it allows placeholder clips. A placeholder motor is not acceptable; the motor stays as merged.

- [x] Read ADR 0074's animation section and `tests/animation_stride.cpp`. List, in the PR, what #86 already checks, so the new tests do not duplicate it. Done when: the PR names those existing checks and does not change their expected numbers.
- [x] Play start, stop and pivot clips from the motor's leg state when the locomotion set has them, and keep today's blend when it does not. Done when: a test selects each clip, a missing clip falls back without a pop, and feet still plant.
- [x] Let an imported profile supply authored gait speeds. The mannequin's measured speeds stay the default when the profile omits them. Done when: `lyko inspect-character` reports the speeds and a malformed speed is refused.
- [x] Standard close. Done when: `02.17d` is ticked. The `02.17` parent stays open until this PR merges; tick the parent in this PR only if every `02.17` child is then done.
  - Done: #89 (`7e67a06`), all five checks green. `02.17d` and `02.17` ticked.

### 2. `02.11b-runtime-scale` — fixture now, run is Joshua's

Prepared chains and scratch are merged (#67). This ID is the target-PC and crowd measurement, not a second solver.

- [x] Add a fixture that poses representative rigs and a crowd through the prepared solver, and reports allocations and per-solve time. Done when: the test fails on a wrong pose or an allocation past the prepared path's budget, and the command is written for the Windows PC.
  - Merged #92 (`74009df`): `tests/ik_runtime_scale.cpp`; command, args and columns in docs/PERFORMANCE.md and the PR.
- [x] Hand the command to Grok's harness notes (`tools/validation/`, Grok owns that directory). Done when: the PR tells Grok the executable, arguments, and columns. You do not edit `tools/validation/`.
- [ ] **Joshua runs this** on the RTX 4070 Windows PC. You do not tick this step.
- [ ] After Joshua accepts the numbers, standard close. Done when: `02.11b-runtime-scale` is ticked. Then tick rollups `02.11b-runtime`, `02.11b` and `02.11` only if no other child is open. Owner feel stays under `02.16`, so `02.11` may stay open until `02.16` if its note still requires that review. Say which you ticked and why.

### 3. `02.12b` — body mass, and moving a body to a pose

Blocked on task 1 unless Joshua reorders. ADR first. Worlds without the new calls keep their hash (ADR 0073).

- [ ] ADR: mass or density on a body, and a call that moves a dynamic body to a pose. Done when: the ADR states units, failure, threading (one thread, as `World` today), and that old hashes are unchanged.
- [ ] Implement it on `physics::World` with no Jolt type in the header. Done when: a test shows a posed body reach the pose, a bad mass refused, and `physics.joints` still passing.
- [ ] Standard close. Done when: `02.12b` is ticked. `02.12` stays open.

### 4. `02.12c` — ragdoll from a skeleton, pose read back

Blocked on `02.12b`.

- [ ] ADR: one body per bone, joints from `02.12a` with limits, and a pose read back from the bodies. Done when: the ADR names limits, what a missing bone does, and that the ragdoll's lifetime follows the world.
- [ ] Build and read back on the mannequin and on one rig from `animation.rig_acceptance`. Done when: the round trip matches within a stated tolerance, and a zero-length bone or a reversed limit is refused.
- [ ] Standard close. Done when: `02.12c` is ticked. `02.12` stays open.

### 5. `02.12d` — blend animation and physics

Blocked on `02.12c`. In the sample and in replay, as the checklist says.

- [ ] ADR: blend weight 0 is the animated pose, 1 is the physics pose, and a value between is defined. Done when: the ADR says who owns the weight, that physics still resolves contacts, and that replay records the weight.
- [ ] Drive it from the sample and from a headless replay. Done when: both paths agree at 30, 60 and 144 Hz, and a weight outside 0..1 is refused.
- [ ] Standard close. Done when: `02.12d` and the `02.12` parent are ticked.

### 6. `02.13` — stumble, recovery, active ragdoll

Blocked on `02.12d`. Motors are this ID, not `02.12` (ADR 0073).

- [ ] ADR: a stumble, a recovery, and motors that drive the ragdoll toward the animated pose. Done when: the ADR states the fixed-seed fixture, what "recovered" means in numbers, and that the feature is off when the profile does not ask for it.
- [ ] Implement the fixture: a fall, a stumble, and a return to the animated pose. Done when: the test fails if the body never leaves the animated pose, and fails if it never returns.
- [ ] Standard close. Done when: `02.13` is ticked.

### 7. `02.14` — first- and third-person camera transitions

Not blocked on ragdolls. `follow_camera.hpp` (ADR 0044) stays the camera. Refine it. Combat lock-on is Grok's `03.03`; do not add it here.

- [x] A transition between first and third person that does not pass through a wall, and does not pop. Done when: an obstruction test and a transition test pass, and today's follow behavior stays when the new path is off.
- [x] Standard close. Done when: `02.14` is ticked.

Merged in #96 (89dc40e): `FollowCameraConfig::transition_speed`, ADR 0075; test `follow_camera` covers transitions, walls and the speed-0 pop.

### 8. `07.03` — lighting, shadows, materials, transparency, post

The current renderer is a lit first path (ADRs 0002, 0025, 0026). This ID is the flagship set, built in slices. Each slice is still one PR under this ID only if it is the whole ID; if you need more than one PR, split the checklist into lettered children in the same PR that introduces them, and do not delete `07.03`.

- [ ] ADR for the first slice you can test headlessly (a stated light, a shadow, a transparent surface, or a post pass). Done when: the ADR names ownership of GPU resources and what a missing shader does.
- [ ] Implement that slice and a golden or numeric image check. Done when: the check fails if the feature is removed, and a software GPU skip stays a skip rather than a pass.
- [ ] Repeat until the ID's list is covered, each under its own child ID if you split it. Done when: `07.03` is ticked only after every named part has a test.

### 9. `07.04` — visibility, batching, GPU resources

After `07.03` has a material identity to batch.

- [ ] ADR: what is culled, what is batched, and how a GPU resource is created and freed. Done when: the ADR states failure and that a leaked resource fails a test.
- [ ] A fixture with enough objects that batching changes the draw count, and culling drops an object behind the camera. Done when: the test reads those counts from the renderer, not from a log scrape that can be empty.
- [ ] Standard close. Done when: `07.04` is ticked.

### 10. `07.05` — terrain, foliage, water, environment rendering

Rendering only. Terrain collision stays on the physics API you already own. Streaming the chunks is GPT's `06.04`. Draw a chunk you are given; do not own the streamer.

- [ ] ADR for one environment surface you can fixture (terrain shading, a foliage batch, or water). Done when: the ADR separates the draw from the physics collision and from GPT's future chunk stream.
- [ ] Implement it with a headless check. Done when: removing the draw fails the check.
- [ ] Standard close, splitting lettered children the same way as `07.03` if one PR cannot cover every surface. Done when: `07.05` is ticked only after each surface you named has a test.

### 11. `06.10` — shader and pipeline hitches

Can start from the current scene. The world-scale hitch number waits on GPT's streaming (`06.04`) and on Joshua's `01.10` run. Do not claim the world is hitch-free.

- [ ] Warm up pipelines for the current fixtures and record a count of compilations during the measured frames. Done when: a test fails if a known shader compiles again during the measured window.
- [ ] Write the column Grok's harness should record. Done when: the PR names the column and does not edit `tools/validation/`.
- [ ] **Joshua runs this** as part of `01.10` / `06.11` once streaming exists. You do not tick the world-scale claim.
- [ ] Standard close for the current-scene warmup only. Done when: the checklist note says the world-scale half is still open if streaming or the PC run is missing. Tick `06.10` only when both halves are done.

### 12. `06.08` — distant worlds and detail transitions

Blocked on GPT `06.01` and `06.04`.

- [ ] Draw a far chunk with a coarser representation, camera-relative (ADR 0021). Done when: a test shows the near and far representations, and the far one is not a full-detail duplicate.
- [ ] Standard close. Done when: `06.08` is ticked.

### 13. `02.16` — character correctness and performance gates

Last character gate. Correctness can be automated. Performance cannot, on this machine.

- [ ] A fixed-seed course check: no persistent ground penetration, replay matches, camera stays out of walls. Extend `player.course` rather than a second course. Done when: the test fails if penetration is allowed to persist.
- [ ] Define the performance columns (frame time, hitch, memory) and the fixture. Done when: the PR names them and points at Grok's harness. You do not edit `tools/validation/`.
- [ ] **Joshua runs this** on the RTX 4070 and accepts the feel. You do not tick this step.
- [ ] After that acceptance, standard close. Done when: `02.16` is ticked. In that PR, tick rollup `02.04` and `02.05` if their only remaining note was owner feel. Finished character art stays out of scope (Track C and the flagship). Tick `02.17` if `02.17d` has already merged and this feel review was the last open note; otherwise leave `02.17` and say why.

## Rollups you close, not features you build

| ID | Tick when |
| --- | --- |
| `02.17` | `02.17d` remainder has merged. Feel review stays noted under `02.16` if that ID is still open. |
| `02.12` | `02.12b`, `02.12c` and `02.12d` are ticked. |
| `02.11b-runtime`, `02.11b` | `02.11b-runtime-scale` is ticked, including Joshua's run. |
| `02.11` | The scale ID is ticked and `02.16` has either ticked or the PR explains that feel remains `02.16`. |
| `02.04`, `02.05` | `02.16` is ticked. Do not rebuild the six states or the crossfade. |

You have no section 10–14 IDs. When this queue is done, stop. Do not pick up Grok's or GPT's later wave.
