# LYKOWARE ENGINE: Codex Brief
Owner: Joshua Kingsworth, Lykoleon Studios. Prepared 2 Nov 2026.
Standard: "No limits. Just excellence." A spirit of excellence in every layer. Goal: higher quality than AAA.

## PRIORITY: Engine first
The engine runtime core comes first and foremost (Phases 0-6). The editor (Track E) and Content Suite (Track C) are later phases built on the finished engine core. Early on, build only minimal debug and CLI tooling (lyko CLI, debug overlay, profiler, screenshot/trace capture).

## CORE RULE: Full ownership
- Lykoleon builds and controls the entire stack: engine, a custom-built editor (NOT Unity/Unreal/Godot/Blender or plugins for them), tools, asset pipeline, AI agent integration. No building inside other people's programs.
- Every part must be promptable and drivable by AI A-Z: CLI + scripting API + text/data formats for every editor action.
- Third-party code only where pragmatic, as low-level permissive-licence libraries (MIT/BSD/Apache/zlib, e.g. Jolt, SDL, Vulkan headers), vendored into our repo as owned forks and wrapped behind our own interfaces so they are replaceable.
- No GPL/copyleft in shipped code. No proprietary SDK lock-in.
- The editor is built from scratch on the engine itself and runs as its own roadmap track (Track E), started after the engine core is finished.
- The editor includes a full built-in 3D content suite (our own 'Blender'): modelling, sculpting, UV/texturing and material painting, rigging, skinning, animation (keyframe + mocap retarget), VFX, terrain/world building, audio editing. No Blender/Maya/Substance dependency. All AI-drivable via CLI/scripting and text/data formats so AI generation tools can import and refine directly. Own roadmap track: Track C (Content Suite).

## 1. Context
- Lykoleon Studios vision: Howler: Initiation (editor, decompiler and upscaler for native ports of classics), Lykoware (in-house engine, this brief), later Pax (anticheat; also the review gate for anything public).
- Repo: GitHub only. The connector currently sees Avrenthrad/lykowareGE. Confirm the final repo name with Joshua before renaming anything.
- Hand-off: Claude may take over when Codex hits limits. Commit and push small, often, with clear messages. Record every significant decision as an ADR in /docs/adr. Keep /docs/STATUS.md current (what is done, in progress, next, known issues).

## 2. Who builds this
Joshua is a one-person studio with the creative vision but limited hands-on skills. AI agents (Codex, Claude, Grok Bot) do essentially all the work: engine, editor, tools, assets, models, textures, animation, VFX, audio, music, voice, writing. Therefore the engine must be AI-native:
- Text/data-based formats for scenes, prefabs, materials, quests, dialogue and config (diffable, human and agent readable; binary only as cooked build output).
- Everything scriptable; editor actions also exposed as commands.
- Headless CLI: build, test, run a scene, capture screenshots, record perf traces, so agents verify their own work.
- Strong automated tests (unit, integration, golden-image screenshot tests, deterministic simulation tests).
- Performance budgets enforced in CI (frame time, hitches, memory).
- Clear module APIs; architecture docs in /docs; ADRs for decisions.

## 3. Scope
- Build for the flagship game first, with a modular core so it can generalise later.
- Must also be able to power private remasters of classic games (e.g. Pokemon Platinum) for personal use only. Anything public is original IP.

## 4. Flagship game requirements
- Massive open-world RPG.
- RTS elements at points: armies, large battles, top-down command view.
- First-person sandbox building and crafting with structural physics.
- Deep first/third-person combat.
- Complex character relationships and faction systems.
- A massively deep main odyssey intertwined with the hero's choices: story and consequence system, branching quest tooling, reputation.

## 5. Character feel
Inspired by GTA/RAGE + Euphoria: physics-driven animation, foot IK on uneven terrain, active ragdoll stumble-and-recover, smooth animation/physics blending.

## 6. Living Memory (legally distinct from WB's Nemesis System, US 10,926,179)
- Do NOT replicate a promoted captain hierarchy whose members return after defeat.
- Instead: faction-wide NPC memory and reputation; rumour propagation; relationship arcs driven by NPC life goals (revenge, alliances, defection, rise to power); world/territory-driven change.
- Patent review by Pax and a patent lawyer before any public release.

## 7. Living World AI
- On-device local LLMs, runnable on RTX 4070-class GPUs, for NPC dialogue, planning and emergent quests/world developments unique per playthrough.
- Constrained by authored lore and the main story spine; fed by Living Memory; guardrails and filters; no server dependency.

## 8. Performance
- Zero stutter: precompiled shaders and pipeline caches.
- Strict frame pacing.
- Scale from MSI Claw handheld (Intel Arc) to RTX 4070 desktop.
- Seamless world streaming, no loading screens.
- ECS data-driven core handling hundreds of units.
- Seamless first/third-person/RTS camera switching.

## 9. Reference engines (study, do not copy)
Godot; Bevy (ECS); UE5 (rendering and tooling bar); id Tech (frame pacing); RAGE/Euphoria (open-world density, character feel); Jolt physics (preferred); PhysX.

## 10. Later / nice-to-have
Built-in Highlighter-style replay capture.

## 11. AI content pipeline
- Art style guide for consistency.
- AI 3D, texture and animation generation plus cleanup passes.
- ElevenLabs (connected) for voice and SFX.
- AI music.

## 12. Rules
- GitHub only for repos.
- No personal data to outside AI models.
- Nothing public without Pax review.
- Any spend needs Joshua's approval: flag paid tools/services before use.

## 13. Recommended stack (judgement call; confirm in ADR-0001)
- Language: C++20 with CMake (already established and verified on Windows and Linux in the lykowareGE repo; native fit for Jolt, Vulkan and AAA middleware). Enforce safety with sanitizers (ASan/UBSan), clang-tidy, warnings-as-errors and strong tests. Record as ADR-0001.
- Graphics: Vulkan primary (cross-vendor incl. Intel Arc, Linux-friendly), via an abstraction leaving room for a D3D12 backend on Windows if Arc driver behaviour demands it. Evaluate wgpu vs raw ash for the RHI.
- Physics: Jolt (via bindings).
- Formats: RON/TOML/JSON for authored data; glTF 2.0 for meshes.

## 14. Roadmap (vertical slices; objective exit criteria per phase)
0 Foundations; 1 Core runtime; 2 Slice 1 character feel on terrain; 3 Combat; 4 Building/crafting; 5 RTS faction battle; 6 World streaming at scale; 7 (moved: editor is Track E after engine core); 8 Living Memory + factions/relationships; 9 Living World AI; 10 Story/consequence tooling; 11 Content pipeline + flagship vertical slice.
Full goals, deliverables, exit criteria and estimates are in /docs/ROADMAP.md (copy from the Lykoware roadmap Page).

## 14b. Track E: Lykoware Editor (from scratch, built on the engine; starts after engine core, Phases 0-6)
Before Track E: only minimal tooling (lyko CLI, debug overlay, profiler, capture). Design the command bus early so it is ready.
- E1: command bus; every editor action is a serialisable text command; CLI and scripting API call the same commands.
- E2: editor shell on engine UI: viewport, scene tree, inspector, undo/redo, hot reload.
- E3: terrain, building, world and streaming tools; animation and physics debug views.
- E4: faction, Living Memory, quest/dialogue graph and story tools (alongside Phases 8-10).
- E5: asset pipeline UI, AI-agent panel, profiling.
Exit criteria per stage: every action reachable by CLI/script with identical result to UI; agent builds a test level end to end headlessly; no dependence on external editors. Est. 1.5-3 years total (judgement call).

## 14c. Track C: Content Suite (our own 'Blender', inside the Lykoware editor; starts after Track E2 editor shell)
Effort estimates are judgement calls for one person + AI agents; each stage ships usable tools before the next.
- C1 Mesh import and inspection + procedural tools (glTF/OBJ/FBX import via permissive libs, mesh stats, normals/UV views, procedural primitives, booleans, modifiers stack as text). Est. 3-6 months.
- C2 Polygon modelling (edit modes, extrude/bevel/loop cut, topology cleanup for AI-generated meshes, retopology, LOD generation). Est. 6-12 months.
- C3 Sculpting + UV/texturing + material painting (multires sculpt, auto-UV/unwrap, PBR layer painting, baking). Est. 9-18 months.
- C4 Rigging, skinning, animation (auto-rig, weight painting, keyframe/curve editor, mocap retarget, IK, ties into Euphoria-style runtime). Est. 9-18 months.
- C5 VFX, terrain/world building, audio editing (particle/VFX graph, terrain sculpt/paint/scatter, audio waveform editing and mixing). Est. 6-12 months.
Exit criteria per stage: every operation callable via CLI/script with results stored in text/data formats; an agent can import an AI-generated asset, clean it and ship it to the engine headlessly; golden-image and round-trip tests pass.
Honest note: Blender-class depth is decades of work; aim for "what the flagship needs, at a high bar" first. Total Track C estimate 3-6 years, after the engine core, overlapping with Track E and Phases 8-11.

## 15. First tasks for Codex (Phase 0)
0. Add the CORE RULE to README and ADR-0000 (Full ownership; licence policy: MIT/BSD/Apache/zlib only, vendored in /third_party, wrapped).
1. Confirm repo name; add README, LICENSE (private), /docs, /docs/adr, STATUS.md.
2. ADR-0001 language, ADR-0002 graphics API, ADR-0003 physics, ADR-0004 data formats.
3. Extend the existing CMake project into modules under engine/: core, ecs, render, physics, input, assets, audio, anim, world, ai, plus tools/cli. Keep the existing foundation and CI; build on it, don't restart.
4. GitHub Actions CI: build, lint, test, headless screenshot test, perf budget check.
5. lyko CLI: build, test, run --scene, screenshot, trace.
6. Commit and push after each step.
