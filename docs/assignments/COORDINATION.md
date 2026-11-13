# Engine work split

Three assistants (Claude, GPT, and Grok) each stay in one chat across steps, read their own assignment, and tick that file as work lands. This page is the map they share. It is not a second checklist to tick.

| Assistant | File |
| --- | --- |
| Claude | [ASSIGNMENT_CLAUDE.md](ASSIGNMENT_CLAUDE.md) |
| GPT / Codex | [ASSIGNMENT_GPT.md](ASSIGNMENT_GPT.md) |
| Grok | [ASSIGNMENT_GROK.md](ASSIGNMENT_GROK.md) |

Read [the roadmap](../ROADMAP.md) and [the master checklist](../MASTER_CHECKLIST.md) with the assignment. Item IDs below are the master checklist's IDs.

## What this split covers

Engine work still open on the master checklist, outside the editor (Track E, section 8) and the built-in content suite (Track C, section 9). Section 15 (Howler, Pax, and the other Lykoleon projects) has its own roadmaps and is out of scope too.

The editor lives in `lykowareEditor`. Nothing in this split is permission to start it, or to start Track C.

## Counts

Checkbox IDs in sections 1–7 of the master checklist: **123**. Before this split, **42** were ticked. #84, #85 and #86 had already merged with all five hosted checks green, and the checklist still said they were unmerged. This split ticks **02.17c** and **02.17e** (44 ticked). **02.17d** stays open: stride matching, lean, gait blend, pivot hold and the profile `locomotion` key are merged in #86; authored start, stop and pivot clips, and imported authored speeds, are not.

A review figure of "14 of 157" is not a count this repository records. The table uses every master-checklist checkbox ID.

| Assistant | Actionable IDs (sections 1–7) | Rollup IDs (tick when the named gate closes; no new feature) | Later engine (sections 10–14) | Also |
| --- | ---: | ---: | ---: | --- |
| Claude | 13 | 7 | 0 | |
| GPT | 20 | 0 | 3 | |
| Grok | 37 | 2 | 44 | plus verification substep `02.02a/02.03a-Windows` |
| **Assigned** | **70** | **9** | **47** | **1** |

79 open IDs remain in sections 1–7 (70 actionable + 9 rollups). Sections 10–14 add 47, all still open. The verification substep sits outside sections 1–7. 70 + 9 + 47 + 1 = 127, which is every remaining engine ID.

Out of scope, not assigned: section 8 (15), section 9 (9), section 15 (4). **28 IDs.**

| | IDs |
| --- | --- |
| Done in sections 1–7, including 02.17c and 02.17e | 44 |
| Open in sections 1–7, each assigned once | 79 |
| Later engine, each assigned once | 47 |
| Open verification substep, assigned to Grok | 1 |
| Out of scope | 28 |

### Claude — 13 actionable

`02.17d` (remainder only), `02.11b-runtime-scale`, `02.12b`, `02.12c`, `02.12d`, `02.13`, `02.14`, `02.16`, `06.08`, `06.10`, `07.03`, `07.04`, `07.05`.

Rollups: `02.04`, `02.05`, `02.11`, `02.11b`, `02.11b-runtime`, `02.12`, `02.17`.

### GPT — 20 actionable

`05.04`, `06.01`, `06.02`, `06.03`, `06.04`, `06.05`, `06.06`, `06.07`, `06.09`, `06.11`, `06.12`, `07.01`, `07.02`, `07.06`, `07.07`, `07.08`, `07.09`, `07.10`, `07.13`, `07.14`.

Later: `14.05`, `14.06`, `14.07`.

### Grok — 37 actionable

`01.09`, `01.10`, `01.11`, `01.12`, `02.15`, `03.01`–`03.11`, `04.01`–`04.09`, `05.01`, `05.02`, `05.03`, `05.05`, `05.06`, `05.07`, `05.08`, `05.09`, `05.10`, `07.11`, `07.12`, `07.15`.

Rollups: `02.02`, `02.03`. Also `02.02a/02.03a-Windows`.

Later: sections 10, 11, 12 and 13 entire, and section 14 except the three IDs GPT owns (`14.01`–`14.04`, `14.08`–`14.12`).

## Already done, and not assigned again

Do not reopen these. The master checklist already records the evidence.

Sections 1 and 2 through `02.12a` and `02.17b`, plus `02.17c` and `02.17e`. In particular, joints (`02.12a`, #81), the locomotion motor (`02.17b`, #83), directional fluidity (`02.17c`, #84), NPCs on that motor (`02.17e`, #85) and stride matching (`02.17d` partial, #86) are on `main`.

`02.02`, `02.03`, `02.04` and `02.05` still have open parent boxes. Their implementation substeps are merged. Those parents are rollups, not new features. Finished character art is Track C and the flagship slice, not a reason to rebuild locomotion states.

## Ragdoll pause

[STATUS](../STATUS.md) and [WORK_BOARD](../WORK_BOARD.md), in the 12 November 2026 locomotion entry: **ragdoll parts 02.12b–d are paused until the locomotion-feel work is done, or until Joshua reorders.** That work is ADR 0074's five PRs. #82–#86 are merged. The only code still open inside it is the `02.17d` remainder (authored start, stop and pivot clips, and imported authored speeds). Claude does that remainder first, then `02.12b`. Joshua can lift the pause earlier by saying so. `02.13` waits on `02.12d` either way. `02.12a` stays as it is.

## Who waits on whom

| Waiting work | Waits on | Why |
| --- | --- | --- |
| Claude `02.12b`–`d`, then `02.13` | Claude `02.17d` remainder, unless Joshua reorders | Owner pause, 12 November 2026 |
| Claude `06.08` | GPT `06.01` and `06.04` | Distant drawing needs region coordinates and a chunk the renderer can ask for |
| Claude `02.16` performance half, `02.11b-runtime-scale` run | Grok's harness, then Joshua on the RTX 4070 | Prep is Claude's fixture; the script and the PC run are not |
| GPT `05.04` | GPT `07.08` | Crowd avoidance sits on the nav API |
| GPT `06.05` physics streaming | Claude, only if unload needs a new physics call | Use today's create/destroy first. Ask Claude for an API instead of editing `physics.cpp` |
| GPT `06.11` | Grok's harness, Joshua's PC | GPT writes the streaming fixture; Grok adds the script hook; Joshua runs it |
| GPT `06.12` | Every Phase 0–6 owner, including Joshua's hardware gates | This is the core acceptance record, not a new system |
| Grok `03.08` navmesh behavior | GPT `07.08` | Until then, enemies use `NpcCharacter::seek` on a fixed path |
| Grok `03.03` | Claude `02.14` only if a shared camera API is required | Combat cameras are new code. Do not rewrite `follow_camera.hpp` |
| Grok `04.06` full collapse | Claude `02.12b` if a piece needs mass or a posed body | A support graph may start on today's bodies and joints |
| Grok `04.07` migration format | GPT `07.10` and `06.07` | A fixture may round-trip through today's scene commands first |
| Grok `05.03`, `05.08` | GPT `07.08` and `05.04` | Formations and large battles need routing |
| Grok `05.07` renderer side | Claude `07.04` if GPU batching is missing | Simulation LOD does not need a renderer change |
| Grok sections 10–13 | GPT `06.12`, and Joshua for creative, legal and hardware gates | Second wave. Do not start during the core |
| GPT `14.05`–`14.07` | The matching section 7 systems, then `06.12` | Same second wave |

`samples/sandbox/main.cpp`, the root `CMakeLists.txt` test list, `engine/CMakeLists.txt` and `tools/cli/CMakeLists.txt` are shared. Add a new file and a few registration lines. Do not rework someone else's target in those files. If two PRs conflict there, rebase.

## Hardware that only Joshua's PC can close

The machine on record is a desktop Windows 11 PC, Intel Core i7-14700F, 16 GB RAM, owner-reported NVIDIA RTX 4070. VRAM and driver are unmeasured. No assistant has that PC.

Grok owns `tools/validation/` and prepares the harness, scripts and instructions for `01.09`, `01.10`, `01.11`, `01.12` and `02.15`. Claude prepares the character and crowd fixtures those scripts launch (`02.11b-runtime-scale`, the performance half of `02.16`). GPT prepares the streaming fixture for `06.11`. Each of those items has a step that says **Joshua runs this**. An assistant never ticks that step. Hosted CI and a Linux VM are not a substitute. The tooling substeps `01.09a`, `01.10a` and `01.11a` are already done; extend them, do not replace them.

## Rules every assistant follows

These are also in each assignment file.

- One branch and one pull request per checklist ID. Keep the PR small.
- Stay in the same chat across steps. Do not checkpoint after every step. Checkpoint only when context is long (about 3–5 PRs) or the assistant is losing track. Do not checkpoint before one PR is open, unless near the context limit.
- At each checkpoint, before "Checkpoint saved — start a new chat", the assistant squash-merges its own open PRs into `main` only when all five CI checks are green and the PR touches only its assigned items. A red or pending PR stays open and is noted. After the merge, it ticks the item and leaves its one-line note. Never merge another assistant's PR. Never close another assistant's PR. Never force-push `main`. If the merge conflicts, rebase on `main`, re-run CI, and merge only once green.
- Tick a step only after its squash-merge, and only when it was merged-ready: it builds, its tests pass, and CI is green on Windows Debug, Windows Release, and the three Linux presets. A local-only result, or a red or pending PR, does not tick the step.
- Add tests with the change. A deliberate failure of the new behavior must be caught.
- Update the master checklist in the same PR, and only for an ID whose done-condition is fully met. Parents and hardware gates stay open until their notes say they can close.
- No new third-party dependency, vendored library, or paid service without Joshua's approval. The repository stays on GitHub. No public release, and no Pax or patent filing, from an assistant.
- Track E, Track C, and section 15 are out of scope. Do not edit another assistant's assignment file. Do not edit this file in a feature PR.
