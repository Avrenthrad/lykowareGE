# ADR 0003 — Physics integration

Status: Proposed; Jolt is preferred in the brief, not yet integrated.

## Options and boundary

Evaluate Jolt under its exact permissive source licence. For Rust compare a thin owned C ABI bridge with maintained binding sources (including all transitive licensing); for C++ use a wrapped native API. Do not expose third-party physics types through game/tool contracts.

Lykoware owns stable IDs, lifecycle, collision filtering, queries, world stepping and event translation. Define safe destruction, callbacks, thread ownership and unit conventions. Deterministic tests have a declared platform/toolchain scope; do not assume cross-platform bit-identical physics.

## Evidence and exit

Vendor a pinned source revision and notices. Verify body create/destroy, fixed-step simulation, queries and character/terrain contact, repeated cleanup, and invalid-handle behavior. Benchmark representative active bodies and structural destruction before promising full-scale construction simulation. Document how active ragdoll/IK will integrate; a rigid-body integration alone is not Euphoria-like animation.
