# ADR 0033 — Audio output through SDL3

Status: Proposed.
Related task/PR: Audio, PR 2 (branch `claude/audio-output`), stacked on PR 1 ([ADR 0032](0032-audio-mixer.md)). PR 3 adds 3D positional sound on ECS entities.
Supersedes / superseded by: none. Turns on SDL's audio subsystem, which [ADR 0008](0008-window-input.md) left off. Closing the last window no longer shuts down all of SDL.

## Context

The mixer (ADR 0032) renders samples into memory. To be heard, they must reach a sound device. The owner chose SDL3 for this: it is already vendored (zlib licence), and it covers WASAPI on Windows and PipeWire, PulseAudio and ALSA on Linux.

Devices pull audio on their own thread. The mixer is single-threaded. CI has no sound card.

## Options

- **How SDL gets the samples:**
  - **SDL's audio-stream callback.** SDL asks for more bytes on its audio thread, and Lykoware renders exactly that much;
  - **queueing from the game thread.** The game renders ahead and pushes the samples. Latency then depends on the frame rate, and a slow frame starves the device.

  The callback was chosen.
- **Sharing the mixer between the game and audio threads:**
  - **a mutex** around every mixer call, held by the audio thread while it renders;
  - **a lock-free command queue** from the game thread to the audio thread.

  The mutex was chosen for now: it is simple and easy to check. Game code holds it for a few calls at a time. A command queue can replace it behind the same `lock()` call if measurements show the audio thread waiting.
- **Testing without a sound card:**
  - **SDL's dummy driver** throws the audio away;
  - **SDL's disk driver** writes the exact device bytes to a file.

  Both are used. Running them without real-time pacing keeps tests fast.

## Decision

Proposed:

- **SDL audio is on** in the vendored build (`cmake/VendorSdl3.cmake`):
  - the dummy and disk drivers are always built;
  - Windows uses WASAPI;
  - Linux loads PipeWire, PulseAudio or ALSA at run time, if their headers were present at configure time;
  - JACK, sndio and OSS are off.
- **`lykoware::audio::Output`** (`lykoware/audio_output.hpp`; no SDL types in the header):
  - **`Output::open(config, error)`** opens the default playback device with a driver: `Platform`, `Dummy`, or `Disk` (which needs a file). Dummy and Disk can run without real-time pacing.
  - **Ownership:** the output owns its `Mixer`.
  - **Starts paused:** voices can be set up before `resume()`; `pause()` stops it again.
  - **`lock()`** returns a `MixerLock`, which is exclusive access to the mixer. SDL's audio thread renders under the same lock. Every mixer call goes through it while the output is open.
  - **The audio thread renders in chunks of 1,024 frames** into a fixed buffer, so it never allocates. It sends 32-bit float stereo at 48 kHz; SDL converts when the device runs at another format.
  - **Diagnostics:** `driver_name()`, and `device_format()` (the device's own rate, channels and whether it takes float).
- **Sessions:**
  - Every output in a process shares one SDL audio session, refcounted, with one driver. Opening an output with a different driver is refused while one is open.
  - The audio and window sessions are now independent. Each stops only its own SDL subsystems. `SDL_Quit` runs only when nothing else uses SDL. Before this, closing the last window called `SDL_Quit`, which would also have stopped audio.

## Contracts and compatibility

- New public header `lykoware/audio_output.hpp`; new source `engine/src/audio_output.cpp`. `window.cpp` now stops only the video and gamepad subsystems, and calls `SDL_Quit` once nothing else is running.
- **Threading:**
  - create, use and destroy an output on one thread;
  - only the mixer is shared with SDL's audio thread, through `lock()`;
  - do not hold a `MixerLock` while destroying the output.
- **Real time:**
  - a `MixerLock` held for long makes the device wait, and the sound may break up;
  - a `Sound` whose last voice ends is freed on the audio thread. Freeing a large sound there could cause a glitch; moving that free to the game thread is future work if it ever shows up in measurements.
- **Lifetime:** the destructor closes the device and waits for any callback in progress, so the audio thread is done with the mixer before the mixer is destroyed.
- **Not yet:**
  - choosing a device other than the default;
  - reacting to devices being plugged in or removed (SDL moves the default device's stream itself);
  - latency settings;
  - recording.

## Consequences

- The engine can make sound on the owner's PC, through WASAPI.
- CI checks the whole path, with the mixer driven by SDL's real audio thread. The disk driver shows that the bytes reaching the device are exactly the mixer's output.
- Locks are simple, but a long hold can cause audible gaps. The rule is documented on `MixerLock`.

## Verification

- **New test `audio.output`** (dummy and disk drivers, no sound card):
  - **Dummy device:**
    - a new output stays paused (nothing rendered after 50 ms);
    - after `resume()`, the audio thread pulls a second of audio;
    - a looping voice keeps playing and a short voice ends on the audio thread;
    - `pause()` stops the pulling.
  - **Concurrent use:** 2,000 rounds of play, pan and stop from the game thread while the audio thread renders.
  - **Session rules:**
    - Disk without a file is refused, and so is a file for another driver;
    - two outputs can share a driver;
    - a different driver is refused while one is open.
  - **Windows and audio are independent:** a dummy window opens and closes while audio plays, and audio keeps playing.
  - **Exact capture:**
    - the disk device's format is 32-bit float stereo at 48 kHz, so SDL converts nothing;
    - three seconds captured from two voices (a 22.05 kHz mono voice at pitch 1.3, a 44.1 kHz stereo voice at pitch 0.9) equal a separate mixer's own output, bit for bit;
    - every rendered frame reached the file.
- **Deliberate breaks:**
  - **Caught, six:**
    - the window quitting all of SDL;
    - the device asking for 16-bit samples;
    - a `pause()` that does nothing;
    - a driver mismatch allowed;
    - an output that starts running;
    - only half the requested bytes rendered.
  - **Not caught, as expected:** removing the Disk-without-a-file check. SDL itself refuses an empty file name, so the check only gives a clearer message.
  - **Not caught by these tests:** a missing lock in the audio callback. ASan cannot see data races.
- **ThreadSanitizer, one-off local build** (GCC 13; `-Wno-tsan` because GCC refuses one of Jolt's atomic fences under TSan as an error):
  - `audio.output` passes with no race reported;
  - with the callback's lock removed, TSan reports data races in `Mixer::render` against `play`, `frames_rendered` and the gain ramps.

  ThreadSanitizer is not a CI job (STATUS: deferred until there are concurrent engine systems). This is the first such system, so a TSan job is worth considering.
- **Local results:** 43/43 pass in the Debug, ASan/UBSan and clang-tidy presets, including `window.lifecycle` and `sandbox.vulkan_window` after the window shutdown change. A GCC `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast` sweep of the new and changed files is clean. In this container SDL found no PipeWire, PulseAudio or ALSA headers, so only the dummy and disk drivers were built; the platform path is unexercised until it runs on a machine with a sound device.
- **CI:** to be recorded.
