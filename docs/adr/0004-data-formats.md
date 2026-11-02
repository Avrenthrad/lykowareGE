# ADR 0004 — Authored data and commands

Status: Accepted — canonical JSON, TOML for hand-edited config, and glTF 2.0 for meshes. No parser is vendored by this decision.

## Context

Machine-written project data and commands must be diffable text that agents and people can read. Hand-edited settings need a format that stays readable in review. C++20 is accepted in ADR 0001, so a Rust-oriented encoding is a poor canonical choice. Source audio, textures and other native media stay separate from text operation histories.

## Options

- Canonical JSON for machine-written records.
- TOML for hand-edited configuration.
- RON. Dropped because it is Rust-oriented.
- glTF 2.0 for meshes.
- Binary only as cooked build output, not as the authored form of those records.

## Decision

Canonical JSON is the format for machine-written data (command envelopes, scenes, prefabs, saves): sorted keys, a schema_version field, stable IDs.

TOML is the format for hand-edited config (project settings, performance budgets).

RON is dropped because it is Rust-oriented.

glTF 2.0 is the format for meshes.

Parsers are hand-written or vendored pinned MIT libraries (nlohmann/json, toml++), chosen when first needed and recorded then.

Binary is only cooked build output.

## Contracts still open

This chooses encodings, not a production schema or file extension. Before persistence is user-facing, specify explicit units and defaults, validation and errors, migrations and round-trip behavior. Keep source/project data, derived runtime content, editor workspace and player saves distinct.

Command envelopes follow the canonical JSON rules: schema_version, command name, validated arguments, target IDs, result or errors, and operation identity. Commands that require replay or undo record ordering and reversible state. Async operations need completion and cancellation semantics. CLI, scripts and later UI call the same operation handlers.

glTF 2.0 here covers meshes. This ADR does not select an animation interchange format, and it does not define field schemas for materials, quests or dialogue.

## Consequences

Do not add RON tooling. Do not vendor nlohmann/json or toml++ until a caller needs a parser; record the exact version and licence at that time. Hand-written parsers remain allowed. Binary GPU-ready data is a cook product, not the authored scene, prefab, save or config format.

## Verification

No representative document has round-tripped yet. Before implementation, add a small scene, command, config and save sample and check invalid input, an unknown schema_version, stable IDs and sorted-key canonical JSON. Golden tests must not assume cooked binary output is bitwise deterministic across devices.
