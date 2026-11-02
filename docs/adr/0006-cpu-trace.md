# ADR 0006 — CPU trace

Status: Accepted for this implementation within the owner's instruction to add a CPU timer, trace, and provisional CI budget.
Related task/PR: branch feat/phase0-cpu-trace-budget, based on PR #5.
Supersedes / superseded by: none. This does not select an authored data format. ADR 0004 accepts canonical JSON, TOML config, and glTF 2.0 meshes; this trace file is a diagnostic Chrome trace, not that authored format.

## Context

Agents need a CPU trace of the sandbox loop before a renderer exists. Screenshot and scene loading still have nothing to capture. A JSON library would be a new dependency for one diagnostic file.

## Options

- Leave `lyko trace` as an unsupported-renderer error until GPU capture exists.
- Record scoped CPU zones in the existing engine library and write Chrome Trace Event JSON by hand.
- Add a separate profiler module or a third-party tracing library.

## Decision

Keep the API in `lykoware_engine`. `CpuTimer` is a steady-clock stopwatch. `CpuTrace` records scoped zones and writes one Chrome trace document without a JSON library. `lyko trace --frames N` launches the sandbox loop, which calls that API. `screenshot` and `run --scene` stay exit 3.

Provisional CPU-only budgets are CTest thresholds in the root `CMakeLists.txt` for the pinned hosted runners windows-2022 and ubuntu-24.04. There is no TOML or JSON budget file. These thresholds are not a hardware profile.

## Contracts and compatibility

Ownership, lifetime, threading, and failure are specified on the declarations in [engine/include/lykoware/trace.hpp](../../engine/include/lykoware/trace.hpp). The trace file is UTF-8 Chrome Trace Event JSON: one object, `displayTimeUnit` of `ms`, and complete (`ph` `X`) CPU events in completion order. Timestamps are steady-clock microseconds from session construction. `pid` and `tid` are the constants 1 because the session is one thread; they are not operating-system identifiers. This file is not a scene, command, or save, and it has no schema version.

## Consequences

Callers can capture the sandbox CPU loop from the CLI. GPU events, process CPU time, and device frame budgets are not provided. The hosted thresholds can fail a slow or hung CPU run, and they can pass a machine that would miss a gameplay frame target. A pass is not a device gate.

## Verification

`trace.api` covers zones, escaping, and write failure. `cli.commands` and `cli.integration` cover `lyko trace --frames`, including a Unicode output path, and keep screenshot and `run --scene` at exit 3. `perf.sandbox_startup`, `perf.cpu_fixture`, and the comparison checks cover the provisional budgets. Record platform results in STATUS rather than treating this plan as a pass.
