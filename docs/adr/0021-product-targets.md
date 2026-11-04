# ADR 0021 — Product targets: desktop PC, single-player built co-op-ready, a 16 km+ world

Status: **Accepted by owner 2026-11-04** (the owner's answers below). The working assumptions marked Proposed are mine and wait for the owner.
Related task/PR: owner's step 4 (hardware profiles, multiplayer, world scope and budgets), asked and answered on 2026-11-04 after the physics spike.
Supersedes / superseded by: supersedes the MSI Claw handheld target in the brief, ENGINE_VISION and PERFORMANCE.md. Earlier ADRs that mention the Claw (0003, 0010, 0017, 0018) keep their text as history; the Claw no longer constrains them.

## Context

The roadmap needs agreed device profiles, multiplayer intent and world scope before budgets and phase gates can be set. ENGINE_VISION listed them as open questions. Two of them change engine structure early:
- Multiplayer decides whether the simulation must be driven only by commands.
- World size decides the precision of positions in physics and rendering.

## Options

Asked as multiple choice, each with its effect stated:

- **Multiplayer:** single-player only; single-player built co-op-ready; co-op from the start; MMO or persistent servers.
- **World size:** zones up to about 2 km; one open world of 4–8 km; a huge open world of 16 km or more; not sure yet.
- **Low-end device:** an MSI Claw model, or none.
- **The RTX 4070:** the minimum or the recommended PC.
- **Frame target on a 4070:** 1080p60, 1440p60, 1440p120 or 4K60.
- **Large-world positions:** double precision now, single precision with a moving origin, or decide later.
- **System RAM:** 16 GB or 32 GB minimum.

## Decision

Owner answers, 2026-11-04:

- **Platform: desktop PC gaming.** The MSI Claw and handhelds are not targets. An Intel Arc desktop GPU is supported like any other GPU that meets ADR 0010's requirements.
- **The RTX 4070 is the recommended PC**, at **1440p and 60 FPS** (16.67 ms per frame).
- **A weaker minimum PC runs with lower settings.** The class offered was about an RTX 3060 or RX 6600. The exact minimum is set by measurement before release.
- **16 GB of system RAM minimum.** The game, including the local AI, must fit in about 10–12 GB of it.
- **Multiplayer: single-player first, built co-op-ready.**
  - The simulation stays deterministic and is driven by per-tick input commands (ADR 0017's fixed step and the Phase 1 command bus), so that co-op for 2–4 players can be added later without a rewrite.
  - No MMO, no persistent servers, and no paid servers.
- **World size: one seamless open world of 16 km or more across.**
- **Positions in double precision now**, before more code depends on single precision. The physics switch is a spike with its own ADR (0022). It re-checks cross-platform determinism and the benchmark before the change merges.

### Working assumptions (Proposed, not owner decisions)

- **Co-op shape:** 2–4 players.
- **Desktop CPU:** at least 6 cores and 12 threads with AVX2 (ADR 0018). The physics benchmark runs on 4 threads, so it is a fair stand-in.
- **VRAM:** the minimum-class cards have 8 GB, so minimum settings must fit in 8 GB. The 4070 has 12 GB.
- **Rendering at 16 km:** GPUs work in single precision, so rendering will be camera-relative: positions are converted to offsets from the camera before reaching the GPU.
- **Frame budgets at 1440p60 on the 4070:** see [PERFORMANCE.md](../PERFORMANCE.md). They stay Proposed until measured on the owner's PC.

## Contracts and compatibility

- **Engine contract:** game-state changes go through commands applied on fixed ticks. Systems must not read wall-clock time, unseeded randomness or thread timing for gameplay results. Replay checks (Phase 1) enforce this.
- **Physics:** ADR 0020's public positions change from `float` to `double` in the ADR 0022 spike. Velocities, sizes and directions stay `float`.
- **Phase gates:** gates that named the handheld (ROADMAP Phase 6, PERFORMANCE.md) now name the recommended 4070 PC and the minimum PC.

## Consequences

- Budgets are for one class of machine, which simplifies tuning and testing.
- Co-op stays possible without network code now, because the command-and-replay work Phase 1 already plans is the base for it.
- A 16 km+ world costs some speed and memory for double-precision positions, and needs camera-relative rendering and streaming designed for it from the start.
- No handheld build means no 30 FPS quality tier.
- **Still open:**
  - traversal speed;
  - active unit and body counts;
  - what persists in saves;
  - terrain digging and destruction scope;
  - the exact minimum GPU;
  - the editor and repository names.

## Verification

- This ADR records decisions; nothing is measured yet.
- Planned:
  - the ADR 0022 spike with double-precision determinism hashes in all five CI jobs;
  - a 16 km precision test (bodies far from the origin behave like bodies near it);
  - frame-time measurements on the owner's RTX 4070 PC once there is a renderer.
