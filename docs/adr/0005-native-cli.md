# ADR 0005 — Minimal native build/test CLI

Status: Accepted for this implementation within the owner's instruction to continue development.
Scope: standalone developer CLI; no engine public API or persisted game format changes.

## Context

Agents need reproducible build/test access before scene execution, rendering or an editor exists. Existing CMake presets and CTest already define those operations.

## Decision

Implement lyko in C++20 as a separate target under tools/cli, reusing CMake/CTest rather than a second build system. Expose build/test with explicit source/preset/tool options. Return child errors, stop after failed configure, and fail a test run that discovers no tests.

Use direct platform process creation rather than shell command strings. Keep UTF-8 CLI data and native Windows wide paths/arguments. Process implementation stays private to the tool; it is not a speculative runtime subsystem.

Do not claim run/scene/screenshot/trace support before the runtime exists; reserved commands return a nonzero unsupported-capability code.

## Contracts and alternatives

See [CLI.md](../CLI.md) for options, defaults, exit codes and limitations. A script-only wrapper would duplicate quoting/platform assumptions and would not establish the native tools target; std::system would introduce shell interpretation. A general engine command bus is unnecessary for this first build/test tool and remains a separate Phase 1 contract.

## Verification

Integration checks launch real CMake/CTest presets and a controlled native tool fixture, exercising cwd, Unicode/spaces/quotes/backslashes/metacharacters, configure/build/test failures, missing tools, invalid options and zero tests. Stage and run the executable on Windows and Linux. Record actual run evidence in STATUS/PR rather than treating this test plan as a pass.
