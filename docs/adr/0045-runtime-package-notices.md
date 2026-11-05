# ADR 0045: Notices in installed runtime packages

Status: Proposed implementation of accepted ADR 0000; no new dependency or licence election.

## Problem and scope

`cmake --install` currently stages binaries, shaders and the JSON library notice. SDL3, Jolt, volk, VMA and Vulkan-Headers notices stay in the source checkout. A copied install tree therefore loses their attributions. Claude flagged this separately from audio/model/texture PRs #37–#43.

Install selected upstream notices and provenance under `share/lykoware/licenses`, including the existing JSON notice and SDL's embedded third-party attributions. Preserve copied files byte for byte; normalize CRLF to LF only in generated embedded excerpts and retain their notice text unchanged. Extract those deterministically from vendored source into build output, never commit generated notices or modify vendor files. HIDAPI remains under its recorded BSD option and stb_image under its MIT option; do not copy the unselected GPL alternative. Preserve existing public-domain/miniz and libm notices faithfully; carrying an upstream notice does not expand the project's licence allowlist or provide a release clearance.

A readable index identifies dependencies, choices and generated excerpts. Build-time tools are not shipped and are not represented as runtime dependencies. Wuffs' own notice installation remains with Claude's #41; test compatibility after applying this patch locally to that stack.

## Checks and failure behavior

A CTest installs to a fresh temporary prefix (including spaces), checks that binaries/shaders and the expected notices exist, compares copied files byte for byte and checks extracted blocks against the source. Test full install and a notices-only install component, plus install-prefix relocation. Missing/changed excerpt anchors fail configuration for explicit review rather than silently omitting an attribution.

The installed directory is the artifact under test. No legal clearance, public release, budget increase, new dependency or adoption of an unselected licence is implied. GitHub CI is currently reported blocked account-wide; record actual local checks and keep Windows/MSVC unverified until checks can run.

## Implementation evidence

PR #45 source `6984cbd`: baseline Linux Debug 42/42 pass; the package component adjustment passes its targeted test. Combined locally with Claude's #44 head `2c41f62`, integration commit `15c0b33` passes 52/52 with software Vulkan and no skips, including Wuffs attribution. Shared CMake changes merge cleanly; work-board prose needs the ordinary reconciliation of parallel task entries. The original full JSON/Wuffs rules are unchanged; supplemental `EXCLUDE_FROM_ALL` rules expose their files in the notice-only component without duplicating normal installs.

Tests check CRLF input, missing anchors, and a deliberate Jolt-install omission. CI run 42036213824 was rejected before any job steps because of account billing/spending settings; it supplies no Windows/compiler evidence. The status stays Proposed, and merge/public-release gates are unchanged.
