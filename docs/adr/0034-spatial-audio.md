# ADR 0034 — 3D positional sound on ECS entities

Status: Proposed.
Related task/PR: Audio, PR 3 (branch `claude/audio-spatial`), stacked on PR 2 ([ADR 0033](0033-audio-output.md)) and PR 1 ([ADR 0032](0032-audio-mixer.md)).
Supersedes / superseded by: none.

## Context

The owner asked for 3D positional sound tied to ECS entities. Sounds should come from where things are, get quieter with distance, and pan left or right around the listener. Like rendering, it must behave the same at the far edge of a 16 km+ world ([ADR 0021](0021-product-targets.md)), and stay deterministic (ADR 0032).

## Options

- **Where voices are tracked:**
  - **a voice handle stored in the ECS component**, kept up to date through a removal hook;
  - **a separate `SpatialAudio` object** that keeps its voices and checks each frame whether their entities still exist.

  The separate object was chosen. The component stays plain data, and no hook has to reach the mixer through `Output`'s lock from wherever an entity is destroyed. A hook firing while game code already holds that lock would deadlock.
- **Distance falloff:**
  - **inverse distance** (6 dB quieter per doubling, as in the real world);
  - **linear;**
  - **exponential.**

  Inverse distance was chosen, with a reference distance (full volume inside it) and a maximum distance (silent beyond it).
- **Direction:**
  - **stereo pan** from the source's direction across the listener;
  - **HRTF** (head-related filtering, which can place sounds behind and above).

  Stereo pan was chosen now. HRTF costs much more and needs per-voice filters the mixer does not have yet.
- **Doppler** (pitch shifting with relative speed) is left out for now. It needs source velocities and a pitch change while a voice plays.

## Decision

Proposed:

- **`lykoware/audio_spatial.hpp`:**
  - **`Listener`:** a position in double precision, plus forward and up. `Listener::looking_at(eye, target, up)` matches `render::Camera`.
  - **`Falloff`:** a reference distance and a maximum distance, with 0 < reference < maximum.
  - **`place(position, listener, falloff)`** returns a gain from 0 to 1 and a pan from -1 to 1:
    - the offset to the listener is computed in double precision before it becomes float (`math::relative_to`, as for camera-relative rendering);
    - gain is the reference distance over the distance (1 inside the reference distance, 0 at or beyond the maximum);
    - pan is the offset's direction projected onto the listener's right (forward × up);
    - behind pans like in front, and a source at the listener is centred.
  - **`AudioSource` ECS component:** sound, gain, pitch, loop and falloff. It is played from the entity's `Transform`.
  - **`SpatialAudio`:**
    - **`play(world, entity, listener, mixer, error, gain_scale)`** starts the entity's source where it is. `gain_scale` is, for example, how hard an impact was.
    - **`update(world, listener, mixer)`**, once per frame, moves each voice's gain and pan to where its entity is now. The mixer ramps the change over 5 ms, so moving sources do not click.
    - **Voices that end** are forgotten.
    - **Entities that go away:** looping voices stop; one-shot voices finish from the entity's last position.
    - **Order:** voices are updated in the order they were started.
- **Determinism:** `audio_spatial.cpp` is built with `-ffp-contract=off` like the mixer. The same calls give bit-identical output, however rendering is split between updates.
- **Sandbox:**
  - **The sound:** a wooden knock made in code at start-up (two decaying low tones plus a burst of filtered noise), so the repository holds no audio files.
  - **Every crate** has an `AudioSource` with its own pitch (0.8 to 1.2), heard fully within 4 m and silent past 80 m.
  - **Impacts:** a crate whose velocity changes by more than 1 m/s in one 60 Hz tick (gravity alone changes it by 0.16 m/s) has hit something. It knocks with a strength of up to 1 at a 6 m/s change, then stays quiet for 0.12 s so a resting crate does not rattle.
  - **The listener** is the camera.
  - **Devices:** the platform's default device; the dummy driver when the window is headless; `--no-audio` turns sound off. Without a device the sandbox runs silently, with a warning.

## Contracts and compatibility

- New public header `lykoware/audio_spatial.hpp`; new source `engine/src/audio_spatial.cpp`. The sandbox gains `--no-audio` and sound in `--window --vulkan`. Its help text no longer describes the old triangle.
- **Threading and locks:** `SpatialAudio` works on a `Mixer`. With an `audio::Output`, call it with the mixer from `Output::lock()`, and hold the lock only for those calls.
- **Units:** distances are in metres, like physics and rendering.
- **Not yet:**
  - Doppler;
  - occlusion, reverb and other environment effects;
  - HRTF;
  - limiting how many voices one source can start;
  - voice priority when all 64 are busy (a new voice is refused);
  - cone-shaped (directional) sources.

## Consequences

- The sandbox now makes sound: crates knock as they land, from where they are on screen.
- The spatial maths is checked exactly, including 16 km out.
- Impact sounds come from a velocity-change heuristic in the sandbox. Contact events from physics would be more exact, and the physics API does not offer them yet.

## Verification

- **New test `audio.spatial`** (no device; exact values):
  - **Placement:**
    - a source at the listener is full and centred;
    - 2 m right gives gain 1/2 and pan 1, and 4 m left gives 1/4 and -1;
    - 3 m right and 4 m ahead gives gain 1/5 and pan 3/5;
    - behind pans like in front;
    - full volume inside the reference distance;
    - just inside the maximum distance is still heard, and at the maximum it is silent;
    - a listener turned to face +X hears +Z on the right;
    - `looking_at` works;
    - placements 16 km out, with coordinates float cannot hold exactly, equal those at the origin bit for bit.
  - **Entities:**
    - a source starts where its entity is;
    - a move ramps from the old place and arrives at the new one;
    - a looping source stops when its entity is destroyed;
    - a one-shot plays on from its entity's last place and is then forgotten;
    - a voice stopped through the mixer is forgotten;
    - `gain_scale` multiplies the gain.
  - **Refusals:** no `AudioSource`, no `Transform`, a bad falloff, `gain_scale` above 16, a gain product above 16, and no sound. Refusals start nothing.
  - **Determinism:** two moving sources and a moving listener over 20 updates give bit-identical output when the frames between updates are rendered as 160 or as 17 + 100 + 43.
- **Deliberate breaks:** 11, each caught:
  - a mirrored pan;
  - linear falloff;
  - no maximum distance;
  - rounding positions to float before subtracting;
  - loops outliving their entity;
  - one-shots cut off with their entity;
  - positions not followed;
  - `gain_scale` ignored;
  - ended voices kept;
  - the falloff unchecked;
  - pan not updated.
- **A weakness found and fixed:** the 16 km test first used coordinates that float holds exactly, so rounding to float too early went unnoticed. It now uses values such as 16000.3.
- **Sandbox:**
  - **ASan/UBSan:** a 1,200-frame headless run under the sanitizers played 30 crate knocks through the dummy driver with no errors.
  - **Recording:** 8.9 s were recorded through the disk driver in a local run, peaking at 0.31 and never clipping. The recording was shared with the owner, not committed.
- **Local results:** 44/44 pass in the Debug, ASan/UBSan and clang-tidy presets. A GCC `-Wshadow -Wconversion` sweep of the new and changed files is clean.
- **CI:** blocked for the whole account (see ADR 0033); to be recorded.
- **Owner check wanted:** run `lykoware_sandbox --window --vulkan` on the desktop with sound on, and listen for the crates.
