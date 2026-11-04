# ADR 0019 — Lykoware Remaster pipeline

Status: Proposed
Related task/PR: documentation only. Personal-use module, not started. Scope: [REMASTER_SCOPE.md](../REMASTER_SCOPE.md). Placement: [ROADMAP.md](../ROADMAP.md).
Supersedes / superseded by: none. Depends on Track E1 (editor command bus) and a later editor MCP server over that bus. Uses the encodings in [ADR 0004](0004-data-formats.md). Bound by [ADR 0000](0000-ownership-and-licensing.md).

## Context

The brief requires the engine to be able to power private remasters of classic games for personal use only. Public work stays original IP. No third-party game assets or extracted content belong in this repository. [ENGINE_VISION.md](../ENGINE_VISION.md) records the same boundary. Howler is described as a future editor, decompiler and upscaler; branding between Howler and this tool is still open.

That workflow is a long pipeline: import a dump the user made from a game he owns, extract it, have an agent map logic, data tables and events into Lykoware text, upscale and remodel assets, rebuild the result through editor commands (faithful, or with a prompt the owner wrote), and keep everything in a local library that records where it came from. The current `lyko` CLI is a process wrapper. It is not the editor command bus, and no MCP server exposes engine commands to agents.

The first importer example is Nintendo DS material whose containers and media are community-documented: NARC archives, NSBMD models, NSBTX textures, SDAT audio, and text or scripts. This record names those formats. It does not include copyrighted bytes, binary layouts, or extraction procedures.

## Options

- **Importers inside the engine.** The runtime would know commercial-game formats and could not ship without this personal workflow. Rejected.
- **Importers inside the editor core.** A public editor build would carry the same personal-use code and the same pressure to ship dumps. Rejected.
- **A separate tool, `tools/remaster/`, with importers per platform and format.** The engine, games, and public editor or store builds do not link it. It calls the editor command bus once that bus and an MCP server exist. This is the proposal.

An engine-wide plugin ABI is a different decision and is not part of this one. Importers register inside the remaster tool only.

## Decision

Proposed. The owner has not accepted this ADR. Nothing here is implemented, and no roadmap box is ticked.

- **Place.** Lykoware Remaster is a future personal-use-only tool at `tools/remaster/`. Engine code does not include its headers, link it, or load its importers. Public builds and any store package omit it.
- **When.** Work starts only after both prerequisites exist and have passed their own exit checks:
  1. The Track E1 editor command bus (serialization, replay, and undo).
  2. An MCP server that exposes that bus to agents.
  Remaster does not implement either prerequisite. It is a client of the bus. The MCP server is an editor-track surface, not a remaster deliverable, and it is not designed here.
- **Importers.** One importer per platform or format family, registered inside the tool. The first example target is Nintendo DS, using community-documented formats (NARC archives, NSBMD/NSBTX models and textures, SDAT audio, text and scripts). Importers read files the user already has. They do not circumvent copy protection. A format that cannot be read without circumvention is out of scope. Committed tests, when they eventually exist, use synthetic archives of original bytes written for the test. Commercial dumps are not fixtures.
- **Stages.**
  1. Import a dump the user made from a game he owns.
  2. Extract it with the matching importer.
  3. AI understand: map logic, data tables and events into Lykoware text ([ADR 0004](0004-data-formats.md)).
  4. AI upscale and remodel assets toward modern quality. The results are derived from the source game and stay personal-only.
  5. AI rebuild through the editor command bus. Faithful mode reproduces the understood result. Prompted mode applies a change the owner wrote, still as validated commands.
  6. Store inputs, derived media, text, and the rebuild in a local Personal Asset Library with provenance tags, including the source game and a personal-only flag.
- **Library.** The library is local and configurable. The default path is `personal-library/` at the repository root, and that path is gitignored. The recommended setting is an absolute directory outside the checkout. Catalog and provenance files follow ADR 0004 and live in the library, not in git. Required tags include the source game, a personal-only flag, the importer, the stage, and a redistribution value of prohibited. Field names are not frozen until a caller exists.
- **AI boundary.** Game content and personal data stay off external AI services ([ADR 0000](0000-ownership-and-licensing.md)). Local inference is the direction that matches that rule. Any paid service needs Joshua's approval before use. This proposal spends nothing.
- **Publication.** Writing this proposal in the private repository is not a release. Importer code may be published only after a security review and a legal review. This ADR is neither review, and it is not legal advice.

## Contracts and compatibility

No engine public API, no on-disk schema in the repository, and no command-bus change. The future tool may depend on engine APIs and on the command bus. The dependency does not run the other way.

Command envelopes the rebuild stage emits follow ADR 0004: `schema_version`, command name, validated arguments, target IDs, result or errors, and operation identity. The bus remains the only mutation path. Remaster does not keep a second copy of editor operations.

Threading and lifetime are unspecified because there is no type yet. The tool is a batch and agent pipeline, not a runtime system. A missing command bus, a missing MCP server, or an input that is not a user-owned personal dump is a hard stop.

Hand-edited library settings, when a caller needs them, are TOML. Machine-written catalogs, provenance, logic maps, tables, events and command logs are canonical JSON. Rebuilt meshes are glTF 2.0. Source audio, textures and other media stay in their native files inside the library ([ADR 0004](0004-data-formats.md)).

## Consequences

The engine core stays free of commercial-game formats. Phases 0–6, Track E and Track C keep their order. Remaster is later and larger than those tracks, and it can slip without blocking the flagship.

Derived meshes, textures, audio and scripts are still copyrighted material from the source game. Upscaling does not turn them into original IP and does not make them committable.

A gitignore entry is a backstop for the default directory. It is not a legal control, and it does not cover a configured path with a different name. The scope doc states the rules that have to hold when the tool exists.

Parsers of untrusted binary formats are a future security risk (archive path escape, unbounded allocations). That is one reason importer code stays unpublished until review.

## Verification

This change is documentation. No tool, importer, dump, extracted asset, or derived asset is added. No checklist box is ticked.

Planned checks for a later implementation, not run here:

- The engine link line and any public or store package manifest omit `tools/remaster/`.
- A run with no dump exits without writing media.
- The library path in use is outside git, or is gitignored, before any write.
- CI contains no ROM, extracted commercial asset, or derived commercial asset.

Evidence for this proposal: the ADR index lists 0019 as Proposed, [REMASTER_SCOPE.md](../REMASTER_SCOPE.md) states the legal guardrails, and `.gitignore` lists `/personal-library/`.
