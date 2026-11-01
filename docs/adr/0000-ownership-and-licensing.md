# ADR 0000 — Full ownership and licensing policy

Status: Accepted policy from the owner-supplied brief.

## Decision

Lykoleon builds and controls original engine/tool/editor code. No Unity/Unreal/Godot/Blender dependency or host-editor plugins. Low-level permissive libraries only: MIT/BSD/Apache/zlib, recorded by exact licence/version. Source forks live under third_party and are wrapped behind replaceable Lykoware APIs. Preserve upstream notices, attribution and modification history. No copyleft in shipped code; no proprietary middleware lock-in.

All tool/editor content operations need CLI and scripting access through the same command contracts. Runtime Phases 0–6 precede editor Track E; Content Track C starts after E2. Early tools stay minimal.

GitHub only; private development. Nothing public without Pax review and owner authorization. Pax does not exist yet; its gate remains an owner-tracked requirement, not an automatic pass. No personal data sent to external AI services. Approval is required before any paid operation. Patent review by a qualified lawyer is also required by the owner before public release of Living Memory.

## Consequences

Vendoring provides source control and replacement ability, not ownership of upstream copyright. Track original and transitive dependencies, licences, notices and integration patches. Source-media formats are allowed; the text-first requirement governs authored project/operation data rather than forcing audio/images into JSON.

The existing C++ foundation is retained until ADR 0001 resolves. No third-party dependency is introduced by this documentation change. Windows toolchains and OS interfaces are distinct from proprietary game middleware; confirm acceptable tooling/platform dependencies before adopting new SDKs.

## Verification

Dependency manifest and licence/notice review required per integration. Review actual distribution contents, including transitive/native dependencies. Validate CLI/UI parity when UI is introduced. Private repository visibility does not itself constitute release review.
