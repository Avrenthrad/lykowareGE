# ADR 0004 — Authored data and commands

Status: Proposed; text-first and agent-accessible policy is established, encoding choices remain open.

## Options

Evaluate TOML/JSON/RON for scene, prefab, material, quest, dialogue, configuration and serialized commands. RON is Rust-oriented; assess independent tooling and migration costs. glTF 2.0 is proposed for mesh/animation interchange. Binary GPU-ready/cooked outputs are allowed. Source audio/textures/native asset data remain separate from text operation histories.

## Shared contract

Specify stable IDs, schema version, explicit units/defaults, validation/errors, deterministic ordering where required, migrations and round-trip behavior. Separate source/project data, derived runtime content, editor workspace and player saves.

Define command envelopes with version, command name, validated arguments, target IDs, result/errors and operation identity. Commands requiring replay/undo must record ordering and reversible state. Async operations need completion/cancellation semantics. CLI/scripts and later UI must call the same operation handlers. This is a contract checklist, not an implemented schema.

## Exit

Choose formats after small representative scene/material/command/save samples round-trip through the chosen language and independent CLI tooling. Verify invalid/unknown-version input, stable references and migration failures. Add golden/canonical data tests without assuming all binary asset output is bitwise deterministic.
