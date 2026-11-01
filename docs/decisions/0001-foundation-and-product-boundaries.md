# ADR 0001 — Foundation and product boundaries

Status: Accepted baseline
Basis: owner-established product names and engine/editor separation; verified Module 01 implementation.

## Context

Lykoleon intends to develop multiple games, a reusable engine, future development tools, and later anti-cheat. Multiple human/AI contributors need a consistent starting point.

## Decision

- This repository develops Lykoware, the Windows-first engine.
- Keep the existing C++20/CMake foundation and separate engine/sample targets.
- Howler: Initiation will provide development tools and consume engine capabilities. Shipped games must run without Howler.
- Keep game-specific rules in game/sample projects, separate from reusable engine code.
- Pax remains a future product; no anti-cheat architecture is selected.
- Store shared contributor instructions and project decisions in the repository.

## Consequences

Future editor tooling can reuse runtime rendering/simulation rather than implementing conflicting versions. Contributors must preserve dependency direction. None of these boundaries commits us to a particular graphics API, ECS, scripting system, network architecture, storage format, or binary plugin interface.

## Evidence

[Module 01 implementation](https://github.com/Avrenthrad/lykowareGE/commit/af55b9baa3fd0fb41bab243a1009d7340efa429f)
and [successful Windows/Linux run](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221).
