# ADR 0005 — Minimal native build/test CLI

Status: Accepted for this implementation within the owner's instruction to continue development.
Scope: standalone developer CLI; no engine public API or persisted game format changes.

## Context

Agents need reproducible build/test access before scene execution, rendering or an editor exists. Existing CMake presets and CTest already define those operations.

## Decision

Implement lyko in C++20 as a separate target under tools/cli, reusing CMake/CTest rather than a second build system. Expose build/test with explicit source/preset/tool options. Return child errors, stop after failed configure, and fail a test run that discovers no tests.

Use direct platform process creation rather than shell command strings. Keep UTF-8 CLI data and native Windows wide paths/arguments. Process implementation stays private to the tool; it is not a speculative runtime subsystem.

Do not claim run/scene/screenshot/trace support before the runtime exists; reserved commands return a nonzero unsupported-capability code.

## Implementation follow-up

`lyko run` with no scene argument launches the existing console sandbox and returns that process's status. This is process startup, not scene loading and not a renderer. `lyko run --scene` and `screenshot` still return exit code 3 and print `unsupported: no renderer`. They must not be described as working capture commands.

`lyko trace --frames N` is a later CPU-only addition. It launches the sandbox loop and writes a Chrome trace. It is not a renderer and not a GPU capture. Its contract is [ADR 0006](0006-cpu-trace.md). The original rule stands for scene loading and screenshots: do not claim them before that runtime exists.

The engine image harness ([ADR 0007](0007-image-harness.md)) is also not `lyko screenshot`. Screenshot, with or without extra arguments, still returns exit code 3 and prints `unsupported: no renderer`. The harness is a CTest-only path.

Subcommands share one dispatcher and the handlers in `tools/cli`. That table is not the Phase 1 serializable command bus. The original decision against a general command bus stands. Header comments on `commands.hpp` and `process.hpp` record ownership, lifetime, threading, and failure.

## Contracts and alternatives

See [CLI.md](../CLI.md) for options, defaults, exit codes and limitations. A script-only wrapper would duplicate quoting/platform assumptions and would not establish the native tools target; std::system would introduce shell interpretation. A general engine command bus is unnecessary for this first build/test tool and remains a separate Phase 1 contract.

## Verification

Integration checks launch real CMake/CTest presets and a controlled native tool fixture, exercising cwd, Unicode/spaces/quotes/backslashes/metacharacters, configure/build/test failures, missing tools, invalid options and zero tests. `cli.commands` checks help text, `run`, `trace --frames`, and exit code 3 for `screenshot` and `run --scene`. Stage and run the executable on Windows and Linux. Record actual run evidence in STATUS/PR rather than treating this test plan as a pass.
