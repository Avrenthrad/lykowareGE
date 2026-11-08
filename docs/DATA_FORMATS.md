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
