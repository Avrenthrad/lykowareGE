# Lykoware Remaster

**Status: proposed. Not started. Personal use only.** Decision: [ADR 0019](adr/0019-remaster-pipeline.md) (Proposed; the owner has not accepted it). Plan: [ROADMAP.md](ROADMAP.md). This document adds no tool, importer, dump, or asset, and it ticks no roadmap box.

## LEGAL GUARDRAILS

**This is not legal advice.** A qualified lawyer has to review publication, distribution, and any question about what the owner may do with a particular game. This repository's contributors are not that review.

These rules govern the module. A future implementation that cannot meet them does not ship, even privately.

- **Personal use only.** Lykoware Remaster exists so the owner can rebuild games he owns, on his own machines, for himself. It is not a product, a store feature, a multiplayer service, or a way to give those games to anyone else.
- **Only dumps the user made from games he owns.** The only input is a dump the user personally made from a physical or lawfully possessed copy he owns. A download, a cartridge the user does not own, or a file whose origin the user cannot attest is refused.
- **Never commit or distribute ROMs, extracted assets, or derived copyrighted assets.** That covers dumps, extracted files, textures, audio, text, scripts, upscaled images, remodelled meshes, and any other output that reproduces a commercial game. Git history is a distribution. So is a pull request, a CI artifact, a package, a chat attachment, and a copy onto another person's machine.
- **The library lives outside the repository.** Provenance catalogs and media live in a configurable local directory. The default path is `personal-library/` at the repository root, listed in `.gitignore`. Prefer an absolute path outside the checkout. A different path inside the working tree has to be gitignored before the tool writes to it. The library is not a git submodule and is not uploaded.
- **Public builds and any store package omit the tool.** Engine games, the public editor, and anything offered to other people build without `tools/remaster/`. A personal local configuration is the only place it is enabled.
- **Importer code may be published only after security review and legal review.** Committing a proposal to this private repository is not publication. Sharing importer source, shipping it, or making it public waits for both reviews. This document is neither review.
- **Do not circumvent copy protection.** Importers read files the user already has. They do not defeat technical protection measures, and a format that cannot be read without that defeat is out of scope.
- **No copyrighted bytes in the tree.** Examples in this document name community-documented formats. They are not excerpts of games. Future tests use small synthetic archives of original bytes written for the test. Commercial dumps are never fixtures, goldens, or samples.
- **No external AI on this material, and no spend, without a separate owner decision.** [ADR 0000](adr/0000-ownership-and-licensing.md) already forbids sending personal data to outside AI services and requires Joshua's approval before any paid operation. Game dumps and derived assets stay on the owner's machines. This proposal authorizes neither a paid call nor an upload.

An attestation flag in the library is a record of what the user claimed. It is not proof, and it is not a licence to redistribute.

## Goals

- Give the owner one personal pipeline from a game he dumped himself to a Lykoware project he can inspect, upscale, and change.
- Keep that pipeline out of the engine core, in `tools/remaster/`, so the flagship and any public build never link it.
- Map imported logic, data tables, and events into the text formats [ADR 0004](adr/0004-data-formats.md) already accepts, so later editor commands can rebuild them.
- Rebuild through the editor command bus, either faithfully or with a change the owner prompted, using the same handlers the editor uses.
- Accumulate a local Personal Asset Library: many games over time, each tagged with its source and a personal-only flag.
- Leave room for one importer per platform and format, starting from community-documented Nintendo DS containers and media.

## Non-goals

- Implementing the tool, any importer, the command bus, or an MCP server in the current phases.
- Shipping, selling, or publicly demonstrating a remaster of someone else's game.
- Putting game rules, commercial formats, or this tool inside `lykoware_engine`.
- An engine plugin ABI, a third-party extension store, or a scripting sandbox for untrusted importer code.
- Circumventing copy protection, sharing dumps, or training a published model on copyrighted game data.
- Replacing Track C. Original art tools stay on Track C. Remaster consumes them later; it does not pull them forward.
- Choosing the Howler product name. The brief describes Howler as editor, decompiler, and upscaler. Whether Remaster is later branded as part of Howler is open. The code location stays `tools/remaster/` either way.
- Freezing JSON field names. Provenance requirements below are the contract; the schema waits for a caller.

## Dependencies

Remaster is sequenced after both of these exist and have passed their own exits. Neither exists today. The current `lyko` CLI is not a substitute ([CLI.md](CLI.md)).

| Prerequisite | Owner | What Remaster needs from it |
| --- | --- | --- |
| Track E1 editor command bus | Editor track | Serializable commands, replay, and undo. Every rebuild mutation is one of those commands. |
| Editor MCP server | Editor track | Agents reach that same bus through the server. Remaster does not embed a second command implementation. |

The MCP server is not specified in ADR 0019. Its job, for this module, is to expose the E1 bus to agents. Designing that server is editor work and is not started.

Until both exist, the allowed work is documentation. No importer skeleton, no sample dump, and no library of real assets.

## Pipeline

Each stage reads the previous stage's files in the Personal Asset Library and writes its own outputs there, with provenance. A stage that cannot satisfy the legal guardrails stops without writing media.

1. **Import.** The user points the tool at a dump he made from a game he owns and attests that fact. The tool records the attestation, the source-game name he supplies, and the platform. It copies or references the dump only inside the library.
2. **Extract.** The importer for that platform unpacks containers into typed files (models, textures, audio, text, scripts, data tables). Unknown chunks are stored as named opaque blobs with their sizes, not silently dropped and not committed.
3. **AI understand.** An agent reads the extracted structure and writes Lykoware text: logic maps, data tables, and events as canonical JSON; hand-edited notes, if a person writes them, as TOML. The text describes the owner's copy. It is still personal-only library content, not a file for this repository.
4. **AI upscale and remodel.** Models, textures, and audio are rebuilt toward modern quality (meshes as glTF 2.0, other media in ordinary source formats). Quality work does not change copyright. Outputs stay in the library and keep the source-game tag.
5. **AI rebuild.** An agent issues editor command-bus operations through the MCP server. **Faithful** mode reproduces the understood result. **Prompted** mode applies a change the owner wrote, recorded in provenance, and every mutation is still a validated command. The bus is the only writer of editor state.
6. **Library record.** The catalog gains an entry the owner can find later: source game, personal-only flag, importer, stages completed, and whether the rebuild was faithful or prompted.

Local inference is the path that matches ADR 0000. A hosted model is a separate decision, with spend approval, and it still cannot be used to upload dumps or personal data unless the owner explicitly changes ADR 0000.

## Plugin importers

Importers are modules inside `tools/remaster/`. Each one declares the platform and formats it reads and the file kinds it emits. The engine does not load them. This is not an engine plugin framework.

The first example target is Nintendo DS. The format names below are community-documented labels so a later importer has a scope. This document does not specify layouts, compression, or encryption.

| Community-documented name | Role in the pipeline |
| --- | --- |
| NARC | Archive container. Extract yields the files packed inside it. |
| NSBMD | Model data. Understand and remodel turn it into glTF 2.0 in the library. |
| NSBTX | Texture data paired with models. Upscale keeps the result in the library. |
| SDAT | Sound archive. Audio stays source media in the library, not JSON. |
| Text and scripts | Game-specific tables and scripts. Understand maps them into canonical JSON events, tables, and logic. |

Further platforms are further importers, each under the same guardrails, each unpublished until the security and legal reviews exist. No platform is authorized by being listed as a future example.

## Personal Asset Library

The library is a growing local collection. One game does not overwrite another. The repository never contains it.

Default path, gitignored:

```text
personal-library/
```

Recommended configuration: an absolute directory outside the checkout. The path is hand-edited TOML when a caller exists (ADR 0004). The gitignore line covers the default name only.

Illustrative layout, not a frozen schema:

```text
personal-library/
  library.toml                 # local settings; personal_only = true
  index.json                   # canonical JSON catalog of entries
  <local-label>/
    provenance.json            # source game, personal-only, importer, stage
    import/                    # the user's dump and the attestation
    extract/                   # importer output
    understood/                # ADR 0004 text
    remodel/                   # glTF and other derived media
    rebuild/                   # command log and the rebuilt project text
```

`<local-label>` is a name the user chooses. The tool does not ship a database of commercial titles.

Provenance on every entry, required once the tool exists:

| Tag | Meaning |
| --- | --- |
| `personal_only` | Must be true. An entry without it is refused. |
| `source_game` | The name the user supplied for the game he owns. |
| `source_platform` | The importer family, such as `nintendo-ds`. |
| `importer_id` | Which importer ran. |
| `stage` | The last stage that completed. |
| `owner_attestation` | The user's statement that this dump is one he made from a game he owns. |
| `rebuild_mode` | `faithful` or `prompted`. Prompted entries also keep the owner's prompt. |
| `redistribution` | `prohibited`. |

Catalog and provenance files are canonical JSON: sorted keys, `schema_version`, stable IDs (ADR 0004). They live in the library because they identify copyrighted inputs. They are not project files for this repository.

## Phased milestones

All of these are planned. None are done. Do not tick them. R1 and later stay blocked until Track E1 and the editor MCP server exist.

| ID | Planned work | Exit, when it is eventually attempted |
| --- | --- | --- |
| R0 | This proposal: ADR 0019, this scope, the roadmap line, the gitignore entry | Docs agree. No tool and no assets. This row is the document you are reading; it is not a completed product milestone. |
| R1 | `tools/remaster/` skeleton, off in every public build, refusing to run without a legal library path | The engine link line omits it. A run with no dump writes no media. |
| R2 | One Nintendo DS importer behind the guardrails, exercised only on synthetic fixtures in git and on the owner's dump locally | CI has no commercial bytes. The local library entry carries provenance. |
| R3 | Understand stage writes ADR 0004 text for tables, events, and logic maps into the library | A reviewer can read the text. It is not committed. |
| R4 | Upscale and remodel into library glTF and source media | Derived files stay in the library and keep `personal_only`. |
| R5 | Faithful rebuild through the command bus via the MCP server | The bus log and a headless load match the understood result. |
| R6 | Prompted rebuild | The owner's prompt is stored in provenance, and every change is a validated command. |
| R7 | Library index across multiple personal entries | The owner can list entries by source game and see the personal-only flag on each. |

R0 being written down is not permission to start R1.

## Risks

- **Copyright and distribution.** The useful outputs are the ones that must not be committed. A single fixture, screenshot, or trace from a real game in CI would break the guardrails.
- **Gitignore is incomplete.** It catches the default directory name. A renamed path, a nested copy, or `git add -f` still publishes the library. The future tool has to refuse a destination that git would track.
- **Copy-protection pressure.** Some commercial files are unreadable unless protection is circumvented. Those files are out of scope, including when a community document explains the protection.
- **Derived work treated as original.** An upscale or a remodel of a commercial asset is still that asset. Prompted changes do not assign the owner a right to distribute it.
- **External models.** Sending a dump, a script, or a texture to a hosted model exports copyrighted material and can export personal data. ADR 0000 forbids that unless the owner changes the rule.
- **Spend.** Upscale and inference services are often paid. Nothing in this scope is approval to spend.
- **Unfaithful understanding.** An agent can misread tables and events. Faithful mode has to be checkable against the extracted source, and the owner accepts a rebuild before treating it as the game.
- **Scope pulled into the engine.** Format parsers, archive code, and AI calls do not belong in `lykoware_engine` or in the public editor.
- **Unsafe parsers.** Archive path escape and huge allocations are expected risks of binary importers. That is part of the security review required before publication.
- **Community documentation is uneven.** Format names are not a specification. An importer follows sources the owner is allowed to use, and it does not copy copyrighted reference code into the tree.
- **Legal review is still undone.** Pax review, patent review, and a lawyer's review of this personal-use pipeline have not happened. Private git hosting is not that review.

## Open decisions

- Owner acceptance of ADR 0019. Until then the status stays Proposed.
- Whether the tool is later named as part of Howler.
- The concrete MCP server contract (editor track, not this module).
- Exact provenance field names and `schema_version`, at the first caller.
- Which local model, if any, performs understand and upscale. No model is selected here.
- Hardware budgets for that local inference, with the profiles still open on the roadmap.

## What was checked for this document

Relative links point at ADR 0019, ADR 0000, ADR 0004, the roadmap, and the CLI note. No code, dependency, dump, or paid call was added. Checklist boxes were left untouched.
