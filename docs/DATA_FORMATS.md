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

## Locomotion motor profiles (ADR 0074)

A locomotion profile is version 1 JSON, at most 64 KiB, read by `locomotion::decode_profile`. Every key is optional except `version`; missing parts keep their defaults; unknown keys, wrong types, duplicate keys and out-of-range values are refused.

- `agility` (0..1), `gait_margin` (0..0.45), `air_control` (m/s², 0..100), `max_lean` (radians, 0..1), `lean_seconds` (0..5);
- `gaits`: `walk`, `jog`, `run`, `sprint` in m/s, strictly rising, each 0.05..20;
- `heavy` and `sharp`: the limits at agility 0 and 1 — `acceleration`, `braking`, `pivot_braking` (m/s²), `turn_rate_rest`, `turn_rate_sprint`, `facing_rate` (degrees/second), `pivot_angle` (degrees, 10..180), `pivot_speed` (m/s, not above the sprint speed), `gait_change_seconds`, `lean` (radians per m/s²);
- `overrides`: any of those limits set directly for the base;
- `variants`: up to 16 named variants, each with its own `agility` and `overrides`.

- `strafe_scale`, `backward_scale` (0.1..1): speed multipliers for sideways and backward movement relative to facing.

A character profile may carry the same keys, except `version`, in an optional `"locomotion"` object (`character::Profile::locomotion`, absent means `nullopt`); the character profile's own `version` covers it. The object is decoded by `locomotion::decode_profile`, so every rule above applies, and any refusal (wrong type, unknown key, a `version` key, out-of-range value, duplicate key) is reported as `Character profile locomotion: <reason>`; the 64 KiB limit covers the whole character profile. `lyko inspect-character` reports `locomotion` as `null` or as `{agility, gaits:{walk,jog,run,sprint}, variants:[names]}`. Callers pass the decoded profile to `PlayerConfig::locomotion`. TOML for hand tuning (ADR 0004) waits on the owner's choice of a reader.

## Character alignment and inspection (ADR 0063)

Shared version-1 profile loading is owned by `character::load_profile/decode_profile`. Optional `yaw_degrees`, `capsule_height`, `capsule_radius`, `eye_height` augment the existing visual-only `height` field. Root and nested duplicates/unknown keys, depth >8 and files >64 KiB are rejected. Fit height/capsule height are 0.1..10 metres; yaw is -180..180 degrees; positive radius must be below half standing height and eye height strictly inside standing height. All values must be finite. Existing profiles retain visual-height semantics.

`lyko inspect-character` emits `lykoware.character-inspection`, schema 1: `ok`, conventions, skin/rig/clip facts, bind bounds, visual alignment, capsule/camera dimensions, warnings and optional pair compatibility issues. Invalid asset/selection returns nonzero; unsupported rig comparison is reported as `compatible:false` without pretending a pose transfer was performed. No GPU is needed. Public APIs use owned standard types; JSON stays behind the engine/tool implementation.

## Authored animation and headless replay

Character profiles optionally reference a relative `animation_graph` path. Graph version 1 and replay version 1, event/socket semantics and the strict retarget subset are documented in [ANIMATION_RUNTIME](ANIMATION_RUNTIME.md), ADR 0065. The CPU CLI and sandbox use the same owned runtime APIs.

## Stationary terrain stance options

Optional character-profile `foot_align_normals`, `foot_max_slope_degrees`, `foot_pelvis`, `foot_max_pelvis_drop` extend existing `foot_ik` settings. Effective policy is reported by inspection; bounds, opt-in dependencies and runtime semantics are in [TERRAIN_STANCE](TERRAIN_STANCE.md), ADR0066.

## Authored walking plant phases and replay

Character profiles optionally enable `foot_locking` and bounded `foot_max_plant_distance`, and with locking `foot_heading_lock` and `foot_max_heading_degrees` (0–90, default 60; ADR 0068), and with locking and `foot_pelvis` the boolean `foot_pelvis_single_support` (ADR 0070), and with that the boolean `foot_balance_shift` and its bound `foot_max_balance_shift` (metres, 0–0.1, default 0.04; ADR 0072). The shared graph carries plant/lift events; the offscreen sample accepts a strict version1 player-input replay. Formats, bounds and current limitations are in [WALKING_FOOT_PLANTS](WALKING_FOOT_PLANTS.md), ADR0067.
