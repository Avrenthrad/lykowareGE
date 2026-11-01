# Vendored dependencies

No third-party runtime source is vendored yet. Before adding a dependency, record:

- Name, upstream URL, exact source commit/version and integrity hash.
- Exact SPDX licence/terms, original copyright and required notices.
- Transitive/native dependencies and distribution obligations.
- Included source path, local patches, wrapper/API owner and update process.
- Build/runtime requirements and verification evidence.

Allowed source licences: MIT/BSD/Apache/zlib under [ADR 0000](../docs/adr/0000-ownership-and-licensing.md). Preserve upstream licence/NOTICE files inside each fork. Do not substitute the root proprietary notice for upstream terms. Existing CI action pins are workflow dependencies, not vendored runtime libraries; track their provenance separately.

Cargo registry use alone does not meet the source-vendoring rule. If Rust is selected, define offline vendoring and checks for transitive crates and native sources.
