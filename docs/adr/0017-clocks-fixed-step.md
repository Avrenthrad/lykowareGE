# ADR 0017 — Clocks and the fixed-step simulation loop

Status: Proposed
Related task/PR: Phase 1 clocks and fixed-step update (branch `claude/clocks`), based on `main` after RHI PR 5c ([#18](https://github.com/Avrenthrad/lykowareGE/pull/18)) merged.
Supersedes / superseded by: none. Builds on [ADR 0006](0006-cpu-trace.md) (CPU timer) and the Phase 1 roadmap.

## Context

Phase 1 lists "clocks/fixed-step update". Its exit tests include command replay that produces equivalent state, and later phases need deterministic combat, physics and battle fixtures. The owner leans towards co-op or an MMO later, which needs a simulation that advances in numbered ticks independent of each machine's frame rate. Frame rates vary: the proposed targets are 60 FPS on the RTX 4070 desktop and 30 FPS on the MSI Claw, and a desktop may run at 144 Hz or more.

## Options

- **Variable time step:** update once per frame by the measured frame time. Simple, but results depend on the frame rate, large frames destabilise physics, and runs cannot be replayed exactly.
- **One update per displayed frame:** ties simulation speed to the display. It breaks on 30 FPS handhelds, variable-refresh screens and headless runs.
- **Fixed step with an accumulator and interpolation:** real time accumulates; the simulation runs whole ticks of a fixed length; drawing blends the last two states by the leftover fraction. Results depend only on the number of ticks, not on the frame rate. This is the standard approach for games that need determinism.

## Decision

Proposed, not accepted by the owner:

- **`lykoware::FixedStep`** (`engine/include/lykoware/clock.hpp`) is the simulation clock.
  - `advance(elapsed_ns)` returns the ticks to run this frame (with their indices) and `alpha`, the leftover fraction of a tick, for drawing only.
  - Tick indices are 64-bit and start at 0. A tick is the simulation's unit of time.
- **Exact time.** Leftover time is kept as an integer in nanoseconds times the tick rate, so one tick is exactly 10⁹ units at any integer rate. One second of input is exactly `ticks_per_second` ticks, with no rounding drift, however long the program runs; a whole-nanosecond accumulator, losing 0.67 ns per tick, would gain a tick about every 116 hours at 60 Hz. The tick count depends only on the total time fed in, not on how it was split into frames.
- **Defaults, all per program:**
  - **60 ticks a second.** This matches the desktop target and runs two ticks per frame on the 30 FPS handheld. The allowed range is 1–1000 Hz, so a server could run at a lower rate.
  - **At most 250 ms of real time per frame.** Longer gaps (a breakpoint, a load, a dragged window) are dropped rather than simulated in one burst.
  - **At most 8 ticks per frame.** A machine that cannot keep up runs the simulation slower instead of falling further behind each frame.
  
  Dropped time is reported per frame and in total.
- **`lykoware::FrameClock`** measures real time between frames with `std::chrono::steady_clock`, never negative. It is separate from `CpuTimer` (ADR 0006), which measures durations for traces.
- **Simulation code** advances only per tick, by the constant `tick_seconds()`, and never reads `alpha` or real time. Drawing may use `alpha`.
- **First caller:** the sandbox's Vulkan window advances its background colour per tick and draws an interpolated blend, so the colour changes at the same speed at any frame rate. It logs the ticks it ran and any dropped time.
- **Deferred until a caller needs them:**
  - pausing, time scaling and single-stepping;
  - recording and replaying inputs per tick (the command bus);
  - a threaded or networked tick.

## Contracts and compatibility

Ownership, lifetime, threading and failure are documented on the declarations. `FixedStep` is a copyable value with no heap memory. `create()` refuses invalid limits with a message, and `advance()` treats negative time as zero. Bit-identical results across frame rates hold for code that uses only tick counts and `tick_seconds()`. Results across compilers, CPUs or floating-point settings are not covered; that scope is declared when replay arrives.

## Consequences

- Simulation results no longer depend on frame rate, and the tick index gives replay and networking a shared timeline.
- Drawing lags real time by up to one tick (about 17 ms at 60 Hz) because it blends the last two ticks; this is the usual trade for smooth motion.
- A simulation tick that costs more than its own length makes the game run slower and reports dropped time, instead of freezing.

## Verification

- `clock.fixed_step` (`tests/clock.cpp`) feeds exact frame times, so it is deterministic. It checks:
  - **Limits:** invalid ones are refused, and the defaults are 60 Hz, 250 ms and 8 ticks per frame.
  - **Exact counts:**
    - one second in 1 ms frames is exactly 60 ticks with nothing left over;
    - an hour of 16,666,667 ns frames is exactly 216,000 ticks;
    - 1000 hours in 10 s frames is exactly 216,000,000 ticks;
    - 1000 Hz and 1 Hz work.
  - **Frame-rate independence:** ten seconds at 30, 60, 144 and 240 FPS and two irregular fixed-seed paces all give 600 ticks, and a stand-in physics state that is bit-identical.
  - **Interpolation:** `alpha` is 0.5 at half a tick, and tick indices are contiguous across frames.
  - **Dropping:** a 1 s stall is clamped to 250 ms (750 ms dropped); 12 ticks owed with a limit of 4 runs 4, drops 8 whole ticks and keeps no backlog; negative time runs nothing.
  - **Tick start times:** exact, and they do not overflow for very late ticks.
  - **`FrameClock`:** measures at least the time slept.
- Local (Linux, GCC 13): 24/24 tests in the Debug, ASan/UBSan and clang-tidy presets. The headless sandbox presented 120 frames in about 80 ms of real time and ran 5 ticks, following real time rather than frame count.
- Deliberate breaks, each caught:
  - a whole-nanosecond accumulator, the classic drift bug, failed 3 assertions, including the 1000-hour count;
  - keeping the backlog instead of dropping it failed 3 assertions.
- Not verified: behaviour on the RTX 4070 and MSI Claw at their real frame rates.
