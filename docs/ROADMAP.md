# Lykoware roadmap

Engine first. Phases 0–6 define the engine core milestone; "finished" means their agreed exit tests pass, not that the engine will never change. Track E begins after Phase 6. Track C begins after E2. Phases 8–10 and relevant editor tools may then develop together.

Remaining engine work, excluding Track E and Track C, is split across three assistants in [docs/assignments](assignments/COORDINATION.md). The editor repo and the content suite are out of that split.

This roadmap supersedes the original module ordering. [CHECKLIST.md](../CHECKLIST.md) retains verified history. Phase 0/1 acceptance remains open; core runtime and early Phase 2 character slices are implemented in the consolidation stack. See STATUS for exact commits and platform evidence. Numeric budgets below reference [PERFORMANCE.md](PERFORMANCE.md) and remain proposals until hardware/profiles are agreed.

## Phase 0 — Foundation and evidence

Goal: settle architecture choices and reproducible agent workflows.
Deliverables: ownership/licence policy; private licence; shared contributor rules; status/roadmap; ADRs 0000–0004; accepted C++20/CMake stack and native-code analysis coverage; initial lyko build/test commands and test fixtures.
Exit: language decision recorded (C++20 accepted); graphics (ADR 0002) and data formats (ADR 0004) accepted as decisions, with Jolt v5.6.0 accepted in ADR 0003 and integrated through ADRs 0020/0022; clean Windows Debug/Release equivalent builds and Linux portability check; lint/unit/integration gates pass; dependency inventory complete; device profiles/budgets agreed. Capture/perf commands have explicit capability/errors; real screenshot tests wait for a renderer. Do not report placeholder subcommands as functional.

## Phase 1 — Core runtime

Goal: drive the runtime without an editor.
Deliverables: lifecycle, window/input, clocks/fixed-step update, ECS with stable IDs, basic rendering/assets/audio, command dispatcher and scripting entry point, CLI scene loading, screenshot and trace capture. APIs expose resource ownership/errors.
Exit: agent loads/runs a text-authored fixture through CLI; resize/focus/close and restart tests pass; command replay produces equivalent state; a real image capture passes its golden tolerance; traces report CPU/GPU timings where supported; initial perf/memory budgets are enforced on calibrated hardware.

## Phase 2 — Character feel on terrain

Goal: controllable first-/third-person character with convincing grounded motion.
Deliverables: terrain collision, foot IK, locomotion transitions, camera obstruction handling, active ragdoll stumble/recovery and animation/physics blending.
Exit: fixed-seed obstacle/slope/step/fall fixtures complete; no persistent ground penetration or camera-through-wall failures; automated replay plus owner-reviewed clips pass; proposed 10-minute run meets target frame/hitch budgets. Test animation/physics stability separately from subjective feel.

## Phase 3 — Combat

Goal: responsive combat built on character control.
Deliverables: first-/third-person attacks, health/damage, timing/hit validation, reactions, enemy behavior and command-driven tuning.
Exit: deterministic combat fixture validates hits/misses, damage, death/restart and animation transitions; simulation continues within the frame budget; owner accepts recorded combat-feel scenarios.

## Phase 4 — Building and crafting

Goal: first-person resource/crafting/construction loop with structural consequences.
Deliverables: inventory/recipes, placement preview/validation, costs, destruction/removal, structural load/support model, physics/nav updates and persistence samples.
Exit: CLI and interactive construction produce equivalent saved state; invalid/cyclic/unsupported structures follow defined rules; collapse and repeated build/remove tests preserve stability/memory; profile a declared structure/body-count fixture within budgets.

## Phase 5 — RTS faction battle

Goal: hundreds of units and top-down commands coexist with direct character control.
Deliverables: selection/orders, navigation, faction teams, battle simulation, scalable AI scheduling and camera switching.
Exit: proposed 256-unit seeded battle runs for 10 minutes at target budgets; units route around modified buildings; first-/third-person/RTS transitions preserve selection/control state; replay and command-state checks pass.

## Phase 6 — World streaming at scale

Goal: traverse a representative seamless persistent world.
Deliverables: async IO/decompression/uploads, terrain/cell streaming, LOD, shader/pipeline preparation, save/load of changed regions, memory residency limits.
Exit: agreed-speed 10-minute traversal with combat/building changes meets warmed hitch/frame/memory budgets on the recommended and minimum desktop profiles (ADR 0021); cold cache behavior is measured separately; no missing collision/corrupt state across streaming boundaries; saves round-trip. Owner accepts core milestone before Track E starts.

## Phase 7 — Reserved/editor moved

No separate runtime phase. The editor is Track E, preventing premature full-editor development.

## Phase 8 — Living Memory, factions and relationships

Goal: persistent reputation and NPC/faction response to world events.
Deliverables: bounded memory/event model, rumours, goals, relationship arcs, territory/faction change, inspection/replay tools.
Exit: scripted multi-step scenarios reproduce documented consequences; save/load preserves memory; simulation/storage budgets hold; excluded captain-hierarchy design is not introduced. Public-release patent/Pax review remains pending until actually completed.

## Phase 9 — Living World AI

Goal: optional local inference enriches authored rules without owning simulation authority.
Deliverables: on-device model adapter, lore/story retrieval, structured proposals, validation, async scheduling, cancellation/timeouts and fallback dialogue/plans.
Exit: no server is needed; RTX 4070-class measured inference fits agreed VRAM/frame budgets; malformed, late, contradictory and unavailable-model cases fall back safely; personal data is not exported; world actions remain validated and recorded. Deterministic replay records accepted proposals rather than assuming repeatable LLM output.

## Phase 10 — Story and consequence

Goal: coherent branching odyssey with authored constraints.
Deliverables: quests/dialogue/state transitions, narrative conditions, relationship integration, textual authoring plus later E4 graphs.
Exit: branching fixtures prove reachability, persistence, failure/recovery and consequential choices; generated events cannot invalidate protected story constraints; CLI/scripts can author and verify scenarios without UI.

## Phase 11 — Content pipeline and flagship slice

Goal: original content demonstrates the systems together.
Deliverables: art guide, licensed/provenanced AI-generation imports, cleanup/cooking pipeline, voice/audio/music adapters, integrated slice and optional replay capture.
Exit: an agent produces/imports/refines/cooks original fixture content using commands; provenance/licence manifest and round-trip/golden tests pass; full gameplay slice meets hardware budgets. Paid services require prior approval; public release remains gated by Pax, owner authorization and required legal review.

## Track E — Custom editor

All stages use engine rendering/runtime and the existing shared commands; no external editor dependency.

| Stage | Deliverables | Exit criteria |
| --- | --- | --- |
| E1 | Harden early command bus for editor operations, serialization/replay and undo contracts | CLI/script invoke identical handlers/results; replay/validation tests pass |
| E2 | Custom shell, viewport, scene tree, inspector, undo/redo, hot reload | Agent builds a test level headlessly; UI/CLI produce equivalent persisted state; undo/redo and reload tests pass |
| E3 | Terrain/building/world/streaming tools and animation/physics inspection | Every operation has CLI/script parity; edited level loads standalone with matching state |
| E4 | Faction/memory/quest/dialogue/story tools alongside Phases 8–10 | Graph/text round trips and command parity pass; authored scenario runs headlessly |
| E5 | Asset pipeline UI, agent panel, profiling | Agent cooks/builds/captures a level end to end; measured output matches headless workflow |

Design and implement minimal command dispatch in Phases 0–1. E1 extends it; do not wait until the editor to make runtime tooling promptable.

## Track C — Built-in content suite

Starts after E2. Each stage is usable and verified before the next.

| Stage | Deliverables | Exit criteria |
| --- | --- | --- |
| C1 | glTF/OBJ inspection, licensed FBX importer evaluation, primitives, booleans, text modifier stack | Agent imports/generates/inspects and cooks a mesh headlessly; topology/UV/normals and round-trip checks pass; no proprietary SDK required |
| C2 | Polygon editing, extrude/bevel/loop cuts, cleanup, retopology, LOD | Representative AI mesh is repaired and shipped via commands; topology and golden tests pass |
| C3 | Multires sculpt, UV unwrap, layered PBR paint, baking | Saved operation/data history recreates expected source and cooked results within tolerances |
| C4 | Rigging/skinning, weights, keyframes/curves, retargeting, IK | Agent rigs/animates a fixture and runs runtime movement; deformation/round-trip/golden checks pass |
| C5 | VFX graph, terrain sculpt/paint/scatter, audio waveform/edit/mix | Each operation is scriptable/serializable; VFX/terrain captures and audio reference tests pass |

Binary source media is retained with text project/operation descriptions; do not pretend arbitrary meshes/images/audio are always compact text. Native tools focus first on flagship needs. No Blender/Maya/Substance dependency is introduced.

## Phase exit evidence

Each exit needs exact commits, commands, fixture versions/seeds, device profiles, actual results and known failures in STATUS/task PRs. Do not progress by marking aspirational checkboxes complete. Scope change or budget adjustment requires an explicit recorded decision.
