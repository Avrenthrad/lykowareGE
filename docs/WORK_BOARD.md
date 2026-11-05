# Shared work board

## Current state

Canonical implementation status: [STATUS.md](STATUS.md).
Canonical plan: [ROADMAP.md](ROADMAP.md).

`main` at `7616ce3` includes Claude's #32–#34 math/mesh/renderer stack (alongside #31 ECS/physics) and Codex's #35 contract hardening and #36 scenes/command replay. Audio is the active task (Claude). Earlier per-task entries remain in pull requests/git history. Details: [STATUS](STATUS.md).

Owner-supplied vision is captured in [ENGINE_VISION.md](ENGINE_VISION.md).

Owner rules for every task: small stacked PRs; commit small and often; keep all five CI jobs green; record each decision as an ADR; keep STATUS handoff-ready after every task; Joshua merges; no regenerable generated files in commits; artifact upload stays opt-in and no storage or service is bought; no roadmap box ticked unless truly done.

Check latest main and open PRs before starting. Documents do not automatically synchronize assistants or reserve work.

## Active task: real capture golden (Claude)

- Owner/status: Claude, active. One PR ([ADR 0043](adr/0043-renderer-golden-capture.md)) in review as #50 on `claude/capture-golden`, stacked on scripting PR 2.
- Objective: the Phase 1 exit item "a real image capture passes its golden tolerance".
- Shared interfaces: a committed golden (`tests/golden/falling-crates-160x120.ppm`) and the `lykoware_image_compare` test helper. A deliberate change to the lit shader, camera, clear colour or the falling-crates sample now needs a reviewed golden update in the same PR (`LYKOWARE_UPDATE_GOLDEN=1`).
- Checks: `scene.capture_golden`, deliberate breaks, five CI jobs once CI runs again.

## Active task: scripting (Claude)

- Owner/status: Claude, active. PR 1 (sandboxed Lua 5.4, [ADR 0041](adr/0041-lua-scripting.md)) in review as #48 on `claude/script-lua`, stacked on window lifecycle. PR 2 (scripts drive scenes through recorded commands; `lyko run --script`, [ADR 0042](adr/0042-script-driven-scenes.md)) in review as #49 on `claude/script-commands`, stacked on PR 1.
- Objective (owner's choice, 2026-11-05: "go with what you recommend"): the Phase 1 scripting entry point, in Lua.
- Shared interfaces: new `lykoware/script.hpp`; new vendored `third_party/lua/` (MIT); `lykoware_lua` linked privately into the engine. PR 2 adds `lykoware/scene_script.hpp`, one read-only method on Codex's `scene::Scene` (`position(id)`, ADR 0031's module), and `lyko run --script/--record`.
- Checks: `script.sandbox`, deliberate breaks, a local fuzz run under ASan/UBSan, five CI jobs once CI runs again.

## Active task: window lifecycle (Claude)

- Owner/status: Claude, active. One PR ([ADR 0040](adr/0040-window-lifecycle.md)) in review as #46 on `claude/window-lifecycle`, stacked on GPU timings.
- Objective (owner's choice, 2026-11-05): the Phase 1 exit item "resize/focus/close and restart tests pass".
- Shared interfaces: `Window::has_focus`/`minimized`, `release_keys_and_mouse_buttons`; the swapchain skips frames while minimised.
- Checks: `window.lifecycle`, `rhi.swapchain`, new `lifecycle.restart` and `lifecycle.restart_vulkan`, deliberate breaks, five CI jobs once CI runs again.

## Active task: GPU timings (Claude)

- Owner/status: Claude, active. One PR ([ADR 0039](adr/0039-gpu-timings.md)) in review as #44 on `claude/gpu-timings`, stacked on textures PR 3.
- Objective (owner's choice, 2026-11-05): the Phase 1 exit item "traces report CPU/GPU timings where supported".
- Shared interfaces: `Device::gpu_timestamps`, `RenderTarget::last_gpu_time_ns`, `Swapchain::take_gpu_frame_times`, `CpuTrace::add_gpu_span`/`elapsed_us`; sandbox `--window --vulkan --trace FILE`; `lyko trace --gpu`.
- Checks: GPU timing checks in the RHI tests (lavapipe has timestamps), `sandbox.gpu_trace`, deliberate breaks, five CI jobs once CI runs again.

## Active task: textures (Claude)

- Owner/status: Claude, active. PR 1 (PNG and JPEG decoding through vendored Wuffs, [ADR 0036](adr/0036-image-decoding.md)) in review as #41 on `claude/image-decode`, stacked on model loading. PR 2 (textures in the RHI and renderer, [ADR 0037](adr/0037-gpu-textures.md)) in review as #42 on `claude/gpu-textures`, stacked on PR 1. PR 3 (glTF materials, texture coordinates and mesh parts, [ADR 0038](adr/0038-gltf-materials.md)) in review as #43 on `claude/gltf-materials`, stacked on PR 2.
- Objective (owner's choice, 2026-11-05): textures on models, in three PRs: (1) image decoding, (2) GPU textures in the RHI and renderer, (3) glTF materials and texture coordinates.
- Shared interfaces: new `lykoware/image_decode.hpp`; new vendored `third_party/wuffs/` (MIT option); PR 2 changes RHI pipelines (descriptor sets) and the lit shader.
- Checks: images encoded by the tests (none committed), deliberate breaks, a cross-check against PIL and a fuzz run (local), five CI jobs once CI runs again.

## Active task: model loading (Claude)

- Owner/status: Claude, active. PR 1 (glTF meshes, [ADR 0035](adr/0035-gltf-meshes.md)) in review on `claude/gltf-meshes`, stacked on audio PR 3 to avoid conflicts in shared files.
- Objective (owner's choice, 2026-11-05): our own glTF 2.0 reader; shapes first, textures later.
- Shared interfaces: new `lykoware/mesh.hpp` and `lykoware/gltf.hpp`; `Renderer::add_mesh`; `MeshRenderer` gains a last field `mesh`; sandbox `--model FILE`.
- Checks: models built in tests (none committed), GPU pixel checks, deliberate breaks, a local fuzz run, five CI jobs once CI runs again.

## Active task: audio (Claude)

- Owner/status: Claude, active. PR 1 (mixer and WAV reader, [ADR 0032](adr/0032-audio-mixer.md)) in review as #37 on `claude/audio-mixer`, based on `main` at `7616ce3`. PR 2 (SDL3 output, [ADR 0033](adr/0033-audio-output.md)) in review as #38 on `claude/audio-output`, stacked on PR 1. PR 3 (3D positional sound and sandbox crate knocks, [ADR 0034](adr/0034-spatial-audio.md)) in review on `claude/audio-spatial`, stacked on PR 2. CI is blocked account-wide since 2026-11-05 (jobs fail before a runner starts); checks are local only until the owner restores it.
- Objective (owner's choice, 2026-11-05): sound playback through SDL3 (already vendored), our own mixer, 3D positional sound tied to ECS entities, deterministic tests.
- Plan: PR 1 the mixer (`lykoware/audio.hpp`, no device); PR 2 output through SDL3 on its audio thread, with dummy-driver tests; PR 3 a listener and an ECS sound component, camera-relative, with procedurally made crate landing sounds in the sandbox (no binary assets).
- Shared interfaces: new `lykoware::audio`; PR 2 turns on SDL's audio subsystem in `cmake/VendorSdl3.cmake`; PR 3 adds an ECS component next to `Transform`.
- Checks: exact-value tests with no device; bit-identical output however rendering is split; deliberate breaks; five CI jobs.

## Completed: review follow-ups (Codex, #35 merged)

- Owner/status: Codex, ready for owner review in #35; all five jobs passed on `f113b45` ([run](https://github.com/Avrenthrad/lykowareGE/actions/runs/41826863572)); original base `5b46f0812eab373fa3e3062acd272c6b593a1b63`, branch `codex/core-contract-hardening`.
- Scope: ECS/physics handle isolation, allocation/exception safety, exclusive physics attachment ownership; ADR 0030. No renderer rewrite.
- Affected interfaces: ECS Entity, physics handles, ECS movement/hook claims, physics component accessors.
- Checks: cross-world rejection; move/locked/rebind tests; injected allocation failures; unchanged deterministic physics hashes; full five-job CI.
- Related work: Claude's `claude/math`, `claude/rhi-mesh`, and `claude/render-scene` supply camera-relative rendering. Keep their ownership and changes intact.
- Implemented on #36, verification in progress: stable scene/schema IDs, fixed-tick replay, render/simulation separation, capture/measurement runner and documentation reconciliation.
- Hardware gate: actual Windows GPU timing and memory on the recommended desktop require that machine. A software Vulkan run is not that evidence.
- Integration: Joshua merges, per the owner rules above. Stacked follow-up PRs may be built/tested together locally before owner merge.

## Completed: rendering work (Claude, #32–#34 merged)

Claude owns #32 (`claude/math`), #33 (`claude/rhi-mesh`) and #34 (`claude/render-scene`). They supply camera-relative math, RHI mesh/depth drawing and the first lit ECS falling-crates renderer. Their CI evidence is recorded in those PRs/STATUS. Codex's scene-command integration preserves that stack and #30's physics slot retirement; it does not replace or publish Claude's branches.

## Completed: scene/replay integration (Codex, #36 merged)

- Owner/status: Codex, implemented and ready for review as #36 (`codex/scene-command-replay`), stacked on #35; preserves Claude's #30 and merged #32–#34. Runtime/scene source `26f7332` passed all five CI jobs (run 41827916577); subsequent documentation/build-concurrency checks must stay green.
- Scope: ADR 0031, versioned JSON box scene/command schema, stable authored IDs, fixed-tick dispatch, render-only snapshots, CLI replay/capture/report, local Windows measurement script and documentation reconciliation.
- Verified on `26f7332`: canonical/malformed-input checks; every tick equal at 30/60/144 Hz/irregular frames; unchanged simulation after interpolation/GPU draw; repeated real CLI captures/hash equality; installed shader paths; five CI jobs green.
- Hardware: owner supplied Windows 11 Home / i7-14700F / 16 GB; graphics device/VRAM and actual measurements pending. Do not record personal machine/user identifiers from the screenshot.
- Integration: [REVIEW_FOLLOWUP](REVIEW_FOLLOWUP.md); Joshua merges. No full editor or later-phase gameplay work.

## Completed task: double-precision positions

Owner: Claude. Done: #26 (ADR 0021), #27 and #28 merged. Jolt and the physics API use double-precision positions (ADR 0022, Proposed); the same scene 16 km and 64 km from the origin matches the origin scene and costs the same; kinematic bodies never sleep. Details and CI runs are in [STATUS.md](STATUS.md).

## Completed task: physics spike

Owner: Claude. Done: #21, #23, #24 and #25 merged. Jolt v5.6.0 vendored with cross-platform determinism (ADR 0018); the `lykoware::physics` API (ADR 0020, accepted by the owner on 2026-11-04) with bodies, height-field terrain and walking characters; a body-count benchmark that also found and fixed a step-memory abort. Details and CI runs are in [STATUS.md](STATUS.md).

## Completed task: physics decision (ADR 0003)

Owner: Claude for the review; the owner decided on 2026-11-04 (#20). Jolt v5.6.0; the *Numerical Recipes*-derived functions are replaced by our own; cross-platform determinism on; floats for now.

## Completed task: Phase 1 clocks and fixed-step update

Owner: Claude. Done: #19 merged (ADR 0017, Proposed). `FixedStep` gives numbered fixed ticks (60 Hz default) with no drift and identical results at any frame rate; `FrameClock` measures real frame time; the sandbox animates per tick. The owner has not accepted ADR 0017 or its 60 Hz default.

## Completed task: Vulkan RHI bring-up

Owner: Claude. Done: PRs #11–#14 and #16–#18 merged, with ADR 0013 in #15. First pixels through a Lykoware-owned Vulkan 1.3 RHI (ADR 0002): instance, device, offscreen render target read back into the image harness, swapchain on the SDL window, Slang shaders compiled and validated at build time, graphics pipelines, and the triangle drawn offscreen (pixel-checked) and in the sandbox window. GPU tests run on Mesa lavapipe in Linux CI and report Skipped on Windows runners, which have no Vulkan. The owner has not accepted ADRs 0009–0012 or 0014–0016; merging them did not accept them. Details and CI runs are in [STATUS.md](STATUS.md) and each pull request.

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

Owner order, set 2026-11-02: (1) Vulkan RHI bring-up, now done; (2) Phase 1 clocks and fixed-step update, now done; (3) decide Jolt in ADR 0003 before any physics, now decided, with the spike as the active task above; (4) agree hardware profiles, multiplayer intent, world scope and budgets, now decided in ADR 0021 (desktop only, RTX 4070 recommended at 1440p60, 16 GB RAM, single-player built co-op-ready, 16 km+ world, double precision). Leave ADR 0008 Proposed until the owner accepts it. No full editor or content suite work before the roadmap gates.

## Codex handoff: review follow-ups

- PR #35: `codex/core-contract-hardening`; source `f113b45` passed all five profiles. Following `6a1f299` updates documentation only.
- PR #36: `codex/scene-command-replay`; runtime/scene source `26f7332` passed all five profiles; subsequent commit records that evidence and adds the two-job host-tool build limit for the owner's 16 GB system. See current PR checks for the exact final head.
- Local: 41/41 CTests pass with Mesa lavapipe (LLVM19), including actual capture; installed replay/capture works from `/tmp`. Broken-link/conflict-marker checks pass. Job count 0 is rejected; 2 configures successfully; CI cache fallback preserves exact tool version/recipe/platform selection.
- No paid services, artifact uploads, generated build files or personal host/user identifiers committed. Upstream JSON source is intentionally vendored with its MIT licence/provenance and staged notice.
- Next: Joshua merges reviewed PRs in dependency order; run `tools/validation/measure-scene.ps1` on the Windows PC and inspect the local reports. Hardware profile/frame pacing/VRAM remain unverified; no Phase 1 exit or flagship budget is claimed.
