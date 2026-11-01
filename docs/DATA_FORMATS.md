# Data ownership and format planning

Status: ownership guidance accepted for planning; concrete schemas, encodings, identifier types, and migration code are not selected or implemented.

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

Before implementing a format, add an ADR and a small representative document covering format/version, identifiers, fields/defaults, load errors, compatibility, and a round-trip check. JSON/TOML/RON and glTF 2.0 are proposals; serialization technology and exact schema/file extensions remain open. No example here is a production schema.
