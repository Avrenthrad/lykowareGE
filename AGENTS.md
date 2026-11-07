# Shared instructions for human and AI contributors

Applies to this repository, including Codex, Claude, Grok, and other assistants. Read this file explicitly if your tool does not load it automatically. Read [CONTRIBUTING.md](CONTRIBUTING.md), [the engine vision](docs/ENGINE_VISION.md), [architecture](docs/ARCHITECTURE.md), and [current work](docs/WORK_BOARD.md) before changing code.

## Source of truth

- User instructions govern scope. Record durable project decisions in the repository; do not rely on another assistant seeing your chat.
- Later owner instructions and accepted ADRs resolve technical details in the master brief. STATUS records implementation, not the brief's aspirations.
- Use the latest repository state and linked task/PR, not remembered status. Distinguish implemented behavior, accepted design direction, and proposals.
- Module 01 is verified. The revised plan is Phase 0 native C++ tooling and remaining stack evaluation, followed by Phases 1–6 runtime; Track E follows Phase 6 and Track C follows E2. Read docs/STATUS.md and docs/ROADMAP.md.
- Do not silently turn unanswered requirements into commitments. List temporary assumptions and when they must be resolved.
- Do not reset existing work when revisiting design.

## Collaboration

- For subsequent work, use a task branch and pull request for implementation and shared interface/format changes. See CONTRIBUTING.md for documentation-only maintenance.
- Give each task one active owner and state its base commit, scope, affected paths/interfaces, and completion checks in the issue or PR. Other assistants may review independently.
- Inspect active PRs before starting overlapping work. Use separate checkouts/worktrees for simultaneous contributors.
- Do not overwrite someone else's changes, force-push shared branches, or merge unrelated work to complete your task.
- Explain interface changes in an architecture decision record under docs/adr (ADR) before dependent work builds on them.
- At handoff, record commit/branch/PR, actual checks and results, assumptions, blockers, and the next action. Use docs/WORK_BOARD.md templates and keep docs/STATUS.md current.
- Maintain [docs/MASTER_CHECKLIST.md](docs/MASTER_CHECKLIST.md) in each relevant PR/handoff: update progress notes, split upcoming steps, and tick only fully verified requirements. Preserve pending merge, Windows, hardware and owner gates. This is Joshua's standing request of 6 November 2026.
- A successful test, review, or merge must have evidence. Do not check off planned or unverified work.

## Code and boundaries

- Accepted stack: C++20/CMake 3.25+ (ADR 0001). Graphics: Vulkan 1.3 from C++ behind an owned RHI (ADR 0002). Authored data: canonical JSON, TOML for hand-edited config, glTF 2.0 meshes (ADR 0004). Jolt v5.6.0 is accepted and integrated natively behind the physics API (ADRs 0003/0020), with double-precision positions (0022). Continue the verified foundation; no Rust/Cargo migration, wgpu/ash evaluation, or RON. Windows x64 primary; Linux secondary. Warnings-as-errors are the default. Linux sanitizer and clang-tidy 18 presets cover current native targets; read STATUS for verification.
- Full ownership and source vendoring: follow docs/adr/0000-ownership-and-licensing.md. No external editor/engine host dependency, copyleft shipped code or proprietary middleware lock-in.
- No paid operations without Joshua's approval; no personal data to external AI services. GitHub-only repositories, private work; no public release without Pax review and owner authorization.
- Every content/tool action needs CLI/script access; minimal debug/capture tooling precedes the editor. Keep authoritative operations shared rather than duplicating UI logic.
- Match existing C++ style: namespace lykoware, four-space indentation, braces on a new line for function bodies, self-contained headers, and explicit standard-library includes.
- Use target-based CMake. Keep public headers under engine/include/lykoware and implementation under engine/src.
- Games and future Howler tools depend on engine APIs. Engine code must not depend on either application.
- Prefer simple ownership and RAII. Document ownership, lifetime, threading, and failure behavior for new public interfaces.
- Keep game rules out of generic runtime systems. Avoid speculative plugin frameworks or subsystem rewrites.
- Pin third-party source versions/commits and CI action SHAs. Document licensing and build/runtime requirements when adding dependencies.
- Keep generated assets, build output, local editor settings, credentials, and proprietary third-party content out of commits.

## Verification and documentation

Windows:
```powershell
cmake --preset windows
cmake --build --preset windows-debug
ctest --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-release
```

Linux:
```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

See docs/BUILDING.md for prerequisites and staged execution. If a platform is unavailable, report that limitation and use CI; never describe a Linux run as Windows verification. Test affected behavior and complete relevant checks. Documentation-only changes require link/path and consistency review; do not add behavior-mirroring tests for prose.

Update CHECKLIST.md only with verified progress. Update architecture, data-format notes, and ADRs when their contracts change. Keep changes focused and record relevant risks or unverified behavior in the PR.
