# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).

`main` at b907aa7 contains PRs #2 and #4–#9, all green on the five CI jobs. Details and evidence: [STATUS.md](STATUS.md). Earlier per-task entries on this board were removed once merged; they remain in the pull requests and git history.
Owner-supplied vision is captured in [ENGINE_VISION.md](ENGINE_VISION.md).

Owner rules for every task: small stacked PRs; commit small and often; keep all five CI jobs green; record each decision as an ADR; keep STATUS handoff-ready after every task; Joshua merges; no regenerable generated files in commits; artifact upload stays opt-in and no storage or service is bought; no roadmap box ticked unless truly done.

Check latest main and open PRs before starting. Documents do not automatically synchronize assistants or reserve work.

## Active task

Vulkan RHI bring-up. Owner: Claude. Status: active; PR 1 in review on `claude/vulkan-rhi-1` (stacked on PR #10), PR 2 in review on `claude/vulkan-rhi-2` (stacked on PR 1), PR 3 in review on `claude/vulkan-rhi-3` (stacked on PR 2), PR 4 in review on `claude/vulkan-rhi-4` (stacked on PR 3). Base: `main` at b907aa7. Delivered as a stack of small PRs; each PR states its own base.

- Objective: first pixels through a Lykoware-owned Vulkan 1.3 RHI (ADR 0002), on the SDL window and headless, with headless output checked by the image harness.
- Planned PRs, in order:
  1. (In review.) Vendor pinned Vulkan-Headers and volk under `third_party/`, with provenance, exact versions and licences. First caller: `rhi::Instance`, loading Vulkan at run time through volk and failing with a clear error when no loader is present. ADR 0009.
  2. (In review.) Physical device selection, logical device and one graphics-and-compute queue, tested on Mesa lavapipe (installed in Linux CI by PR 1). ADR 0010.
  3. (In review.) Vendor Vulkan Memory Allocator 3.4.0 with its first caller: an offscreen render target, a dynamic-rendering clear, read back through `ImageCapture` and compared exactly by the image harness (headless path). ADR 0011.
  4. (In review.) Swapchain on the SDL window presenting the same clear (interactive path through the same RHI), two frames in flight, resize rebuilds; headless offscreen Vulkan window so CI runs the present path. ADR 0012.
  5. Shader compiler: owner chose **Slang** (ADR 0013, 2026-11-03); spike passed on Linux; owner approved the CI Actions cache.
     - 5a (in review, branch `claude/vulkan-rhi-5a`): vendor Slang, SPIRV-Tools and SPIRV-Headers; host tools built once into a cache; build-time `slangc -O0` → `spirv-opt` → `spirv-val`; test shader compiled every build. ADR 0014.
     - 5b (in review, branch `claude/vulkan-rhi-5b`): `rhi::Shader`, `rhi::GraphicsPipeline`, `RenderTarget::draw`; device enables `shaderDrawParameters`; triangle drawn and checked by the image harness. ADR 0015.
     - 5c (next): the sandbox window draws the triangle; decide how the runtime finds shader files.
- Scope and affected paths: `third_party/`, `engine/include/lykoware/rhi/`, `engine/src/rhi/`, tests, CMake, CI workflow, docs and ADRs.
- Shared interfaces/formats affected: new public RHI API; each PR documents ownership, lifetime, threading and failure on its declarations.
- Related tasks/PRs and dependencies: ADR 0002 (accepted), ADR 0007 image harness, ADR 0008 window surface hook. Independent of ADR 0003.
- Acceptance checks: all five CI jobs green on every PR; no Vulkan link-time dependency on machines without a driver; headless golden compare passes wherever a Vulkan driver is available, and is reported as skipped (not passed) where it is not.
- Assumptions and decisions needed: lavapipe on GitHub-hosted Ubuntu runners uses distribution packages, which costs nothing; confirm before relying on it. Windows-hosted runners have no GPU, so Windows graphics tests may be compile-only until there is a driver. Shader-compiler choice needs a licence review before PR 5.

## Task template

Copy into an issue or PR before implementation:

- Task/module:
- Owner and status (planned / active / blocked / review / done):
- Base commit and branch:
- Objective:
- Scope and affected paths:
- Shared interfaces/formats affected:
- Related tasks/PRs and dependencies:
- Acceptance checks:
- Assumptions and decisions needed:

## Handoff template

Use in the task/PR when pausing, completing, or transferring work:

- Task, branch, commit, PR:
- Completed behavior:
- Files/interfaces changed:
- Checks run and actual results (link CI where available):
- Unverified behavior/platforms:
- Open decisions and blockers:
- Next concrete action:
- Checklist/documentation updates:

## Recorded task: accept ADR 0002 and ADR 0004

- Task/module: Phase 0 documentation. Accept the graphics and data-format ADRs and correct the brief.
- Owner and status: done. The content reached `main` through the merged #5–#8 stack; [PR #4](https://github.com/Avrenthrad/lykowareGE/pull/4) itself was closed. No runtime owner.
- Base commit and branch: `850bd50451ecf108e353716fca24db5a685ddd00` on `main`; branch `docs/accept-adr-0002-0004`.
- Objective: Record Vulkan 1.3 behind a Lykoware-owned RHI, and canonical JSON / TOML / glTF 2.0, and make the brief agree.
- Scope and affected paths: documentation only. ADRs 0002 and 0004, the ADR index, the brief, STATUS, this board, and the planning pages that still described the old proposals.
- Shared interfaces/formats affected: graphics API direction and authored-data encodings. No schema, renderer, parser or vendored source.
- Related tasks/PRs and dependencies: ADR 0001 is accepted. ADR 0003 stays proposed. wgpu, ash and RON are rejected as Rust-oriented.
- Acceptance checks: the brief, ADR index, STATUS and this board name the same decisions; no code, dependency or spend; Windows and Linux CI still build the existing foundation.
- Assumptions and decisions needed: shader-compiler licence review and a software Vulkan driver such as Mesa lavapipe in CI are follow-ups, not accepted requirements. D3D12 only if Intel Arc measurements justify it. Parser libraries are chosen when first needed. Jolt is not selected by the brief's native-C++ wording.

### Handoff

- Task, branch, commit, PR: accept ADR 0002 and ADR 0004; branch `docs/accept-adr-0002-0004`; decision commit `0d9100ff73271a3828eaae55951a2f43a203d112`; pull request https://github.com/Avrenthrad/lykowareGE/pull/4.
- Completed behavior: ADR 0002 and ADR 0004 are Accepted on this branch. The brief no longer evaluates wgpu against ash, no longer puts Jolt behind bindings, and no longer lists RON as a format.
- Files/interfaces changed: documentation listed in the task. No public API or on-disk schema was implemented.
- Checks run and actual results: relative links in the edited Markdown resolve. Local Linux `cmake --preset linux-debug`, `cmake --build --preset linux-debug`, and `ctest --preset linux-debug` passed (`foundation.smoke`). Pull-request CI [run 41325777090](https://github.com/Avrenthrad/lykowareGE/actions/runs/41325777090) on `d3c4e4535f698b31b7acdd212cabbe9b69d98ca3`: Linux portability check passed. Windows Debug and Release configure, build, test, stage, and staged executable passed. Both Windows jobs then failed on `actions/upload-artifact` because the account artifact storage quota is exhausted. No storage was purchased. That quota is not freed immediately by deleting artifacts.
- Unverified behavior/platforms: no renderer, capture path, parser round-trip or vendored library was exercised. The Windows failure is the artifact upload, not the build or test.
- Open decisions and blockers: ADR 0003 physics; multiplayer and hardware profiles; shader-compiler licence; lavapipe (or another software Vulkan driver) as a CI follow-up; exact parser choice at first use.
- Next concrete action (at the time): Phase 1 window/input/timing. Superseded by the owner order under "Suggested next task". Graphics dependencies are vendored with their first caller in the Vulkan RHI task. ADR 0003 stays open.
- Checklist/documentation updates: graphics and data-format acceptance is checked. Physics acceptance stays open. STATUS next steps match this handoff.

## Suggested next task

Owner order, set 2026-11-02: (1) Vulkan RHI bring-up, the active task above; (2) Phase 1 clocks and fixed-step update; (3) decide Jolt in ADR 0003 before any physics; (4) agree hardware profiles (RTX 4070 desktop, MSI Claw), multiplayer intent, world scope and budgets. Leave ADR 0008 Proposed until the owner accepts it. No full editor or content suite work before the roadmap gates.
