# ADR 0032 — The audio mixer: deterministic voices, our own WAV reader

Status: Proposed.
Related task/PR: Audio, PR 1 (branch `claude/audio-mixer`). PR 2 sends the mix to a device through SDL3; PR 3 adds 3D positional sound on ECS entities.
Supersedes / superseded by: none. The first part of Phase 1's "basic rendering/assets/audio" deliverable ([ROADMAP](../ROADMAP.md)).

## Context

Audio is the last Phase 1 system. The owner chose (2026-11-04): playback through SDL3, which is already vendored ([ADR 0008](0008-window-input.md)); our own mixer; 3D positional sound tied to ECS entities; deterministic tests.

The engine is built to be deterministic and command-driven, so it can become co-op later ([ADR 0021](0021-product-targets.md)). Tests compare exact values, not "sounds about right". The mixer therefore needs output that can be checked exactly in CI, which has no sound card.

## Options

- **Mixing:**
  - **SDL3's audio streams** mix and resample for us. They are quick to adopt, but their results depend on SDL's internals and version, and cannot be checked bit for bit;
  - **a third-party mixer** (miniaudio, SoLoud). These are larger dependencies, and the 3D and resampling choices would be theirs;
  - **our own mixer** writing float samples into memory; SDL only plays them.

  Our own was chosen, as the owner asked. It is small and can be tested exactly without a device.
- **Decoding:**
  - **WAV now, read by our own code.** WAV is uncompressed and the format is simple.
  - **Compressed formats** (Ogg Vorbis, Opus, FLAC) through a permissive library, e.g. stb_vorbis (public domain/MIT) or dr_flac. They are deferred until real content needs them.
- **Resampling:**
  - **linear interpolation** with integer fixed-point positions;
  - **a windowed-sinc or polyphase filter.** It sounds better at large pitch changes but costs more.

  Linear interpolation was chosen for now. A better filter can replace it behind the same API, with new expected values in the tests.
- **Panning:**
  - **constant power** (sin/cos or square roots);
  - **linear.** Its sound dips about 3 dB in the centre.

  Constant power was chosen. It uses square roots, not sin/cos: IEEE 754 requires sqrt to be correctly rounded, but not sin and cos.

## Decision

Proposed:

- **`lykoware::audio`** (`lykoware/audio.hpp`):
  - **Output:** 48 kHz, stereo, interleaved 32-bit float.
- **`Sound`:** immutable decoded audio, shared by every voice that plays it.
  - **Format:** mono or stereo; 8–192 kHz; samples finite and in [-1, 1]; at most an hour at 192 kHz.
  - **`Sound::create`** makes one from samples.
  - **`Sound::load_wav`** reads WAV files with our own code:
    - PCM 8, 16, 24 or 32 bit, or 32-bit float;
    - mono or stereo;
    - plain WAVE or `WAVE_FORMAT_EXTENSIBLE` (its subformat GUID is checked);
    - unknown chunks are skipped, with odd-size padding.

    Truncated and malformed files are refused. Finite floats that go past ±1 are clamped, since export tools often produce them. NaN and infinity are refused.
- **`Mixer`:**
  - **Voices:** up to 64. `play` returns a generation-checked `VoiceId`, under the same handle rules as ECS entities and physics bodies; a slot whose generation would wrap is retired.
  - **Controls:** `stop`, `playing`, `set_gain` (0–16), `set_pan` (-1 to 1) and `set_master_gain`. Gain and pan changes ramp over 240 frames (5 ms) and land on the target exactly, so they do not click. `pitch` (1/8 to 8) is set when a voice starts.
  - **Mono voices** are panned with constant power: left² + right² = gain².
  - **Stereo voices** are balanced: panning turns down the far channel only.
  - **`render(out)`** overwrites `out` with the next frames.
    - Voices are summed in slot order, then multiplied by the master gain and clamped to [-1, 1].
    - A voice that reaches its end stops. A looping voice wraps exactly, and interpolates across the loop seam to its first frame.
- **Determinism:**
  - **The same calls give bit-identical output** on every supported compiler and CPU, however the output is split into `render` calls.
  - **Playback positions are 32.32 fixed-point integers.** Pitch and sample rate become one integer step, rounded once at `play`.
  - **Sample maths uses only +, -, *, / and sqrt.** IEEE 754 rounds these the same everywhere.
  - **No fused multiply-adds:**
    - GCC and Clang build `audio.cpp` with `-ffp-contract=off`, so a multiply and an add are never fused into one instruction with a single rounding;
    - MSVC's default `/fp:precise` does not fuse them.
  - **A new voice takes the lowest free slot.** If it took the most recently freed one, the slot (and so the summing order) would depend on which `render` call each voice ended in.
- **Threading:** one thread at a time. The device layer in PR 2 calls `render` from SDL's audio thread under a lock.

## Contracts and compatibility

- New public header `lykoware/audio.hpp`; new source `engine/src/audio.cpp`. Nothing else in the engine changes.
- **Failures:**
  - `play` and the `Sound` factories return empty and fill an error string;
  - functions taking a `VoiceId` return false for a stale handle or an out-of-range value;
  - nothing throws for misuse (`std::bad_alloc` can propagate).
- **Lifetime:** a voice holds a `shared_ptr` to its sound, so dropping the caller's pointer while the voice plays is safe.
- **No device yet:** SDL3's audio subsystem stays off (`SDL_AUDIO OFF` in `cmake/VendorSdl3.cmake`) until PR 2 ([ADR 0033](0033-audio-output.md)).
- **Not yet:**
  - output to a device (PR 2);
  - listener, distance attenuation and the ECS sound component (PR 3);
  - compressed formats;
  - streaming long sounds from disk;
  - buses, effects and filters;
  - a better resampler;
  - more than stereo output.

## Consequences

- Audio results can be tested exactly in CI without a sound card. A change in mixing shows up as a failed comparison, not as a person noticing a difference.
- Linear interpolation dulls high frequencies at large pitch changes. That is acceptable for effects now; music and dialogue may need a better resampler later.
- Each frame costs a few multiplies per voice. A full mix of 64 voices is far below one core's budget; PR 2 measures it on real output.

## Verification

- **New test `audio.mixer`** (no device; exact comparisons):
  - **Sound validation:** channels, rates, empty sounds, partial frames, NaN and out-of-range samples are refused.
  - **Panning and mixing:**
    - a centred mono voice gives `0.5 × sqrt(0.5)` in each channel;
    - pan ±1 is fully left or right;
    - voices add, each scaled by its gain;
    - the mix is clamped;
    - the master gain ramps and then scales the mix;
    - stereo balance turns down the far channel only.
  - **Ends and handles:**
    - a voice plays its last frame and then stops;
    - stale and default handles are refused;
    - a freed slot is reused with a new generation;
    - bad parameters, a 65th voice and odd output sizes are refused.
  - **Looping and resampling:**
    - a loop wraps exactly;
    - a 24 kHz sound is interpolated linearly;
    - the loop seam interpolates back to the first frame;
    - pitch 2 skips every other frame.
  - **Ramps:** a gain change falls steadily and lands exactly on the target.
  - **Determinism:**
    - two voices with a pan change part-way, rendered as 300 + 700 frames and as seven uneven pieces, give bit-identical output;
    - voices that end in different calls leave the next voice in the same slot, and the later mix bit-identical.
  - **WAV:**
    - 16-bit stereo, 24-bit extensible (sign-extended), 8-bit unsigned, 32-bit float (overshoot clamped) and 32-bit integer files load with exact sample values;
    - a padded odd-size chunk is skipped correctly;
    - no RIFF header, truncated data, ADPCM, six channels, partial frames, a 4 kHz rate, no chunks, an unknown extensible GUID and infinite floats are refused.
- **Deliberate breaks:** 14, each caught:
  - a linear pan law;
  - gains that jump instead of ramping;
  - a ramp that misses its target;
  - the highest free slot instead of the lowest;
  - no interpolation across the loop seam;
  - the master ramp advanced once per call instead of once per frame;
  - no 24-bit sign extension;
  - stereo panned like mono;
  - no output clamp;
  - no GUID check;
  - no chunk padding;
  - a wrong interpolation scale;
  - voices that never stop;
  - infinite floats clamped instead of refused.
- **A determinism bug found and fixed during review:** the first version reused the most recently freed slot. Two voices ending in a different order across calls would then put the next voice in a different slot and change the summing order. It now takes the lowest free slot, and `audio.mixer` checks this.
- **Local results:** 42/42 pass in the Debug, ASan/UBSan and clang-tidy presets. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast` sweep of the new files is clean.
- **CI:** to be recorded from the pull request.
