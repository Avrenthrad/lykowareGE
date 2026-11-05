# ADR 0041 — Scripting: sandboxed Lua 5.4

Status: Proposed. The owner asked for the recommended option on 2026-11-05 ("just go with what you recommend").
Related task/PR: scripting PR 1 of 2 (branch `claude/script-lua`), stacked on window lifecycle ([ADR 0040](0040-window-lifecycle.md)).
Supersedes / superseded by: none.

## Context

Phase 1's deliverables include "a command dispatcher and scripting entry point". The dispatcher exists: versioned scene commands with replay ([ADR 0031](0031-scenes-and-command-replay.md)). Nothing yet lets game logic or an agent run code against the engine without rebuilding it.

Scripts in this engine will be written by the developers and by AI agents. They have to be:

- **Contained:** an endless loop or runaway memory in a script must not hang or crash the engine, and scripts must not reach files.
- **Repeatable:** the engine relies on deterministic replays.

## Options

- **Language:**
  - **Lua 5.4** (MIT). A small C library, the most used embedded language in games, and easy to sandbox; agents write it well.
  - **Luau** (MIT). Lua with gradual types and a built-in sandbox, but a larger C++ codebase, and its versioning follows Roblox.
  - **Wren, Squirrel or AngelScript** (MIT, MIT and zlib). Smaller communities, and less known to agents.
  - **Our own language.** Months of work before it does what Lua already does.
  - **C++ only for now.** No scripting entry point, which leaves the deliverable open.

  Lua 5.4 was recommended, and the owner chose the recommendation.
- **Version:**
  - **5.4.9**, the latest bug-fix release of the 5.4 line;
  - **5.5** (5.5.0, 2025-12-15), newer, with global declarations and an incremental major collector.

  5.4 was chosen for its longer record in games. 5.5 is a possible later upgrade, behind the same wrapper.
- **Containment:**
  - **a sandbox inside one Lua state**: only safe libraries, memory and instruction limits;
  - **a separate process per script.**

  The in-state sandbox was chosen: scripts are trusted not to be hostile for now (see Consequences), and it needs no process management.

## Decision

Proposed:

- **Vendored:** Lua 5.4.9 sources in `third_party/lua/` (see its PROVENANCE).
  - They are compiled as C++ in one Lykoware translation unit, so Lua errors are C++ exceptions and unwind host-function frames properly.
  - The string hash seed is fixed.
  - Only the core, the auxiliary library and the base, coroutine, string, table, math and utf8 libraries are compiled.
  - The MIT notice is installed in `share/lykoware/licenses/lua`.
- **`lykoware/script.hpp`:** `lykoware::script::Script` owns one sandboxed state. No Lua type appears in the header.
  - `create(ScriptConfig)` takes a memory limit (default 16 MiB, at least 256 KiB), an instruction limit (default 100 million, in steps of 1,000) and a random seed.
  - `run(source, chunk_name)` runs Lua text; bytecode is refused (`"t"` mode).
  - `call(name, arguments, results)` calls a global function.
  - `define(name, HostFunction)` makes a C++ function callable from scripts.
  - `set_print(sink)` takes print output; by default it is logged.
  - `memory_used()` and `instructions_used()` report usage.
- **Values** crossing in either direction are `Value`: nil, boolean, integer, number or string. Integers and floats stay apart. Tables, functions, coroutines and userdata are refused, naming the argument or result.
- **The sandbox:**
  - **Kept:** the base functions, plus the coroutine, string, table, math and utf8 libraries.
  - **Removed:**
    - no io, os, package or debug library, so no `require`;
    - no `load`, `loadfile`, `dofile`, `collectgarbage` or `string.dump`;
    - no `math.randomseed`: the host seeds `math.random` from `ScriptConfig::random_seed`.
  - **Finalizers (`__gc`)** are refused by `setmetatable`, because Lua runs them with hooks off, beyond the instruction limit.
- **Limits:**
  - **Memory:** a counting allocator refuses any allocation past the limit. Scripts see "not enough memory" and the state stays usable.
  - **Instructions:** a count hook raises an error at the limit and on every later step.
    - `pcall`, `xpcall` and `coroutine.resume` are wrapped to pass that stop on, so a script cannot catch it and carry on.
    - The wrappers use continuations, so coroutines still yield across `pcall`.
  - **Loops inside Lua's C library:** these run no Lua instructions, so the count cannot stop them. Fuzzing found a script hanging in `string.rep('', 2^40)`, which loops 2^40 times while allocating nothing. So the library calls that could loop without end while using no memory are bounded:
    - `string.rep` of an empty string (and separator) returns `""` at once;
    - `table.insert` and `table.remove` refuse a table whose length is beyond what the memory limit could hold. A sparse table, or a `__len` metamethod, can report a length of 2^40 for 41 elements;
    - `table.move` refuses a range of more elements than that.

    A pattern match on a pathological pattern can still run very long (see Not yet).
  - **Re-entry:** `run`, `call` and `define` are refused from inside a host function.
- **Errors:** host-function failures, and `std::exception`s they throw, become Lua errors at the call site with the script's line, which scripts may catch with `pcall`. Every Lua API use that can fail runs in protected mode, so a failure is reported, never a panic.

## Contracts and compatibility

- **New:**
  - `lykoware/script.hpp`;
  - the `lykoware_lua` static target, linked privately into the engine;
  - the `script.sandbox` test;
  - the installed Lua notice.
- **Determinism:** the same script given the same calls gives the same results on the same build. Two things fall outside that:
  - iteration over keys that are tables or functions follows memory addresses;
  - `math.sin` and similar come from the platform's C library, so they can differ in the last bit between platforms.

  Numbers print in the C locale; the engine never sets a locale.
- **Not yet** (scripting PR 2 and later):
  - scripts issuing scene commands;
  - a `--script` entry point in the sandbox and `lyko`;
  - tables across the boundary;
  - per-call time limits;
  - a defence against hostile scripts. A pattern match with heavy backtracking (`string.find`, `match`, `gmatch`, `gsub`) is one instruction, however long it runs. Bounding it needs a change to the vendored `lstrlib.c` or a separate process.

## Consequences

- C++ game code and agents can run Lua against the engine through one small API, with limits that keep a broken script from taking the engine down.
- Mods or downloaded content would need a stronger sandbox (a time limit per call, and limits inside the string library) before running untrusted scripts. That is a separate decision.

## Verification

- **`script.sandbox`** (new):
  - **Running:** chunks run; print output reaches the sink tab-separated; globals persist; integers stay integers.
  - **Errors:**
    - syntax and runtime errors name `test.lua:<line>`;
    - a non-string error object is described;
    - the state is usable after errors.
  - **The sandbox:**
    - 13 names are absent: `io`, `os`, `debug`, `package`, `require`, `dofile`, `loadfile`, `load`, `collectgarbage`, `string.dump`, `math.randomseed`, `module`, `loadstring`;
    - the kept libraries are present;
    - bytecode and `__gc` are refused.
  - **Instruction limit:** stops `while true do end` within one step of the limit, through `pcall`, `xpcall`, `coroutine.resume`, a wrapped coroutine and a `__close` handler. A coroutine still yields across `pcall`.
  - **Library loops:**
    - `string.rep('', 2^40)` returns at once;
    - `table.move({}, 1, 2^40, 2)` is refused;
    - `table.insert` and `table.remove` on a sparse table with a length of 2^40, and through a `__len` that claims 2^40, are refused;
    - the same functions still work on real tables.
  - **Memory limit:** stops a growing table and a 1 GB string, never passing the limit. The state is usable afterwards. Limits below their minimums are refused.
  - **Host functions:**
    - all five value kinds pass through;
    - failures carry the script line and are catchable with `pcall`;
    - a table argument is refused;
    - a thrown `std::exception` becomes a script error;
    - re-entry is refused;
    - bad names and empty functions are refused.
  - **`call`:**
    - arguments and results pass through;
    - missing functions, table results and errors are reported;
    - a global lookup that runs script code is protected.
  - **Determinism:** two states with one seed iterate 200 string keys and draw 20 random numbers identically. Another seed draws other numbers.
- **Deliberate breaks:** 25 of 25 caught.
  - **Sandbox:**
    - the hash seed from time and addresses;
    - `load`, `string.dump` or `math.randomseed` left in;
    - bytecode accepted;
    - `math.random` not seeded by the host;
    - `__gc` allowed.
  - **Instruction limit:**
    - `pcall`, `xpcall` or `coroutine.resume` unguarded;
    - the guard without a continuation;
    - the stop raised only once;
    - the count not reset per run.
  - **Memory limit:** ignored.
  - **Library loops:**
    - `string.rep` unguarded;
    - `table.insert`/`remove` unguarded;
    - `table.move` unguarded;
    - the element limit too high.
  - **Host boundary:**
    - re-entry allowed;
    - tables accepted as nil;
    - integers read as floats;
    - a host exception escaping;
    - the global lookup unprotected;
    - the error position dropped;
    - a keyword accepted as a name.

  Four of these first exposed weak tests, which were fixed:
  - the coroutine stop came late;
  - the "raise every step" behaviour was untested;
  - the hash-seed test let two states share one address;
  - the bytecode test matched any binary error.
- **Fuzzing** (local): 200,000 mutated scripts through `run` and `call` under ASan/UBSan, with random memory and instruction limits, in 192 s, with no crash and no hang.
  - An earlier run hung on `string.rep('', 2^40)`, which led to the library-loop guards.
  - The slowest script took 1 s: a string-allocating loop stopped by its instruction limit under ASan.
- **Local results:** 54/54 pass in the Debug, ASan/UBSan and clang-tidy presets. The handle-ownership fixes from #47 are merged in.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
