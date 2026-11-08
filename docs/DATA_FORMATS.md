# Data ownership and format planning

Status: ownership guidance and encodings are accepted for planning. [ADR 0004](adr/0004-data-formats.md) selects canonical JSON, TOML for hand-edited config, and glTF 2.0 for meshes. Concrete schemas, identifier types beyond stable IDs, and migration code are not implemented.

## Separate data categories

| Category | Examples | Author / consumer |
| --- | --- | --- |
| Source assets | Original models, textures, audio | Creators/tools; asset build pipeline |
| Game project content | Scenes, object definitions, materials, scripts, starting values | Howler or text tools author; engine/toolchain consume |
| Derived runtime assets | Converted meshes/textures and packaged content | Build pipeline produces; runtime loads |
| Editor workspace | Panel positions, viewport state, local preferences | Howler only; local settings normally ignored by Git |
| Player saves | Inventory, quests, player-built objects, changed world state | Running game through engine facilities |
| Build configuration | Target platform, packaging options, import settings | Versioned project/toolchain configuration |

A building placed by the developer belongs to authored project content. A building constructed by the player belongs to save state. Editor workspace must not determine shipped gameplay.

All authored scenes, prefabs, materials, quests, dialogue, configuration and operation histories must be diffable text/data. Cooked build output may be binary; source media keeps suitable native formats. See [ADR 0004](adr/0004-data-formats.md).

## Requirements for future formats

- Identify assets/objects durably rather than serializing pointers or temporary process indices.
- Define explicit schema/version handling and behavior for unsupported versions.
- Validate external data and report missing references clearly.
- Preserve authoring sources; distinguish reproducible derived output from source.
- Define migration and recovery behavior before persistence becomes user-facing.
- Document coordinate conventions, units, defaults, references, and unknown-field behavior.
- Keep saves, authored content, and editor workspace distinct even if they eventually share serialization utilities.
- Avoid serializing editor-only metadata into required game runtime structures.

## Decision gate

Before implementing a format, add a small representative document covering format/version, identifiers, fields/defaults, load errors, compatibility, and a round-trip check. [ADR 0004](adr/0004-data-formats.md) accepts canonical JSON for machine-written data (sorted keys, a schema_version field, stable IDs), TOML for hand-edited config, and glTF 2.0 for meshes. RON is dropped. Parsers are hand-written or pinned MIT libraries chosen when first needed. Exact field schemas and file extensions are not implemented. No example here is a production schema.

The image harness writes diagnostic PPM P6 fixtures. That file is not an authored scene, texture source, or save. See [ADR 0007](adr/0007-image-harness.md).

## Character locomotion profile additions (ADR 0062)

Version 1 character profiles retain `model`, idle/walk, height, bones and stationary foot-IK settings. Optional `run_clip`, `jump_clip`, `fall_clip`, `land_clip` are nonempty strings naming unique imported animation clips. Missing run uses walk; missing airborne/landing clips use idle. Invalid/unknown/ambiguous selectors are refused. These select presentation clips; they do not author a general state graph, configure thresholds, retarget a rig or drive root motion. See [BUILDING](BUILDING.md#locomotion-clips-and-transitions).

## Character alignment and inspection (ADR 0063)

Shared version-1 profile loading is owned by `character::load_profile/decode_profile`. Optional `yaw_degrees`, `capsule_height`, `capsule_radius`, `eye_height` augment the existing visual-only `height` field. Root and nested duplicates/unknown keys, depth >8 and files >64 KiB are rejected. Fit height/capsule height are 0.1..10 metres; yaw is -180..180 degrees; positive radius must be below half standing height and eye height strictly inside standing height. All values must be finite. Existing profiles retain visual-height semantics.

`lyko inspect-character` emits `lykoware.character-inspection`, schema 1: `ok`, conventions, skin/rig/clip facts, bind bounds, visual alignment, capsule/camera dimensions, warnings and optional pair compatibility issues. Invalid asset/selection returns nonzero; unsupported rig comparison is reported as `compatible:false` without pretending a pose transfer was performed. No GPU is needed. Public APIs use owned standard types; JSON stays behind the engine/tool implementation.

## Authored animation and headless replay

Character profiles optionally reference a relative `animation_graph` path. Graph version 1 and replay version 1, event/socket semantics and the strict retarget subset are documented in [ANIMATION_RUNTIME](ANIMATION_RUNTIME.md), ADR 0065. The CPU CLI and sandbox use the same owned runtime APIs.

## Stationary terrain stance options

Optional character-profile `foot_align_normals`, `foot_max_slope_degrees`, `foot_pelvis`, `foot_max_pelvis_drop` extend existing `foot_ik` settings. Effective policy is reported by inspection; bounds, opt-in dependencies and runtime semantics are in [TERRAIN_STANCE](TERRAIN_STANCE.md), ADR0066.
