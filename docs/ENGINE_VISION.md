# Lykoware engine vision

Owner: Joshua Kingsworth, Lykoleon Studios.
Source: [owner-supplied Grok brief](LYKOWARE_BRIEF.md), labelled 2 November 2026, maintained with subsequent owner decisions. This document summarizes requirements; accepted ADRs resolve technical choices.

## Standard and priorities

"No limits. Just excellence." Higher-than-AAA quality is an aspiration; release readiness is established by measured phase exits, playtesting, and review.

Engine first: runtime Phases 0–6 precede Track E (editor). Track C (content suite) begins after E2. Until then, build only the lyko CLI, scripting/command access, debug overlay, profiling, screenshot/trace capture, and small test fixtures.

## Full ownership

Lykoleon owns its original implementation and controls its build, runtime, custom tools, asset pipeline, and agent integration. Do not build inside or depend on Unity, Unreal, Godot, Blender, or plugins for them.

Low-level libraries may be used pragmatically only under MIT/BSD/Apache/zlib licences, including approved variants with their exact terms recorded. Vendor source forks under third_party and wrap them behind Lykoware interfaces. Upstream copyright and licence obligations remain; vendoring does not transfer ownership. No GPL/copyleft in shipped code or proprietary middleware lock-in. See [ADR 0000](adr/0000-ownership-and-licensing.md).

Every content/editor operation must eventually be invocable through serializable commands and scripting as well as UI. Authored data is diffable text/data; cooked runtime output may be binary. Large source media retains its native representation and cannot always be expressed as text.

## Product map

- Lykoleon Studios: studio.
- Lykoware: this engine; repository remains Avrenthrad/lykowareGE until Joshua confirms a different name.
- Howler: Initiation: future custom editor, decompiler, and upscaler; precise branding between Howler and the brief's "Lykoware Editor" remains open.
- Pax: future anti-cheat, and the owner's required review gate before anything public. No Pax implementation exists.
- Individual games: content and game rules using the runtime.

GitHub is the only repository host. Keep work private. Do not publish without Pax review and owner authorization. Do not transmit personal data to outside AI services. Any spend requires Joshua's approval before using the paid service.

## Flagship capabilities

- Massive seamless open-world RPG; no routine loading screens during traversal.
- First-/third-person combat and first-person sandbox building/crafting with structural physics.
- RTS command camera, armies, hundreds of simulated units, and large battles.
- Smooth first-person/third-person/RTS camera transitions.
- Physics-driven animation: terrain foot IK, active ragdoll stumble/recovery, controlled animation/physics blending.
- Relationships, factions, reputation, branching quests and consequential main-story spine.
- Living Memory: faction-wide reputation/memory, rumour spread, NPC goals and relationship arcs, territory/world-driven change. Do not reproduce the promoted-captain return-after-defeat hierarchy described in the brief. No patent clearance is established by this design description; the owner requires Pax and patent-lawyer review before public release.
- Living World AI: local dialogue/planning/emergent events constrained by authored lore and story, using Living Memory. No server dependency; target RTX 4070-class inference hardware with explicit frame-time/VRAM budgets and safe fallbacks.
- Desktop PC only: RTX 4070 recommended at 1440p60, a weaker minimum at lower settings, 16 GB RAM (ADR 0021). The MSI Claw handheld is no longer a target.
- Optional later replay/highlighter capture.

## Content and reference direction

Build for the flagship first and generalize through modular boundaries. Private classic-game remaster/port workflows are a future capability; public work is original IP. No third-party game assets or extracted content are included in this repository.

Study Godot, Bevy, UE5, id Tech, RAGE/Euphoria, Jolt and PhysX as references; do not copy incompatible code or assume their features exist here.

Later Track C provides original modelling, sculpting, UV/texturing/material painting, rigging/skinning, animation/retargeting, VFX, terrain tools and audio editing. No required external DCC application. AI-generation integrations must preserve provenance, art direction and licensing. ElevenLabs is a requested future option, not a verified connection or authorization to spend.

## Confirmed implementation versus proposals

The foundation includes native C++20/CMake tools, SDL input/windowing, Vulkan RHI, clocks, Jolt physics and the owned ECS/physics link. Consolidation PR #65 includes audio, scripts, command replay/capture, models/materials, GPU timing, character controls, skeletal animation, GPU skinning, imported profiles and stationary terrain alignment. See STATUS and GitHub for exact integration state and verification; implementation does not close hardware or phase gates.
Not implemented: the full editor/content suite, large-world streaming, gameplay systems and local AI.
Language/graphics: accepted C++20/CMake and Vulkan 1.3 behind owned APIs; no Rust migration. D3D12 remains a measurement-driven contingency.
Physics: Jolt v5.6.0 accepted (ADR 0003), native C++ API (0020), double-precision positions (0022).
Data: canonical JSON, TOML config and glTF 2.0 accepted (0004). The first box-scene and command schemas are proposed in ADR 0031; a generic content/save schema is future work.
See [ADRs](adr/README.md), [roadmap](ROADMAP.md), and [status](STATUS.md).

## Remaining product questions

- Answered 2026-11-04 ([ADR 0021](adr/0021-product-targets.md)): single-player first, built co-op-ready; desktop PC with the RTX 4070 recommended at 1440p60 and 16 GB RAM; one seamless world of 16 km or more, with double-precision positions.
- Exact minimum GPU, desktop CPU and memory budgets per system?
- Traversal speed, active-unit/body counts, and persistence expectations?
- Terrain digging/deformation, destruction granularity, and structural simulation scope?
- Howler/editor naming and final repository name?
