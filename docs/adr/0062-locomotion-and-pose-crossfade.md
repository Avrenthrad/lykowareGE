# ADR 0062 — Interruptible pose crossfades and locomotion sample

Status: Proposed. Owner: Codex. Date: 8 November 2026. Base main `b17e479`.

## Engine contract

Add `animation::PoseCrossfade`, a bounded (1..max_joints), owned presentation-pose transition. `reset` validates/copies an initial pose, allocating reusable buffers at setup; `begin` captures the latest visible pose and selects a finite 0..3600 second fade. `advance` blends this captured source towards a live sampled target, validates complete spans and finite nonnegative time, and commits visible pose/time/output only after validation. Input/output spans may overlap. Rotations use normalized shortest-arc interpolation; scales must remain finite/nonzero. Invalid operations preserve visible state/time and caller output. Successful begin/advance reuse storage; separate mutable instances are required for concurrent characters. No game state, root motion, physics mutation, animation graph or replay determinism guarantee is added.

## Sample behavior and authored data

Keep locomotion decisions in `samples/course`, outside the engine. Presentation states are idle, walk, run, jump/rise, fall and land. Ground state, horizontal speed and vertical velocity come from PlayerCharacter. Speed/apex hysteresis reduces threshold chatter; observed airborne-to-ground transitions enter a short landing state. Initial grounding never fabricates a landing. Negative/nonfinite drawing time advances nothing; drawing delta is capped at 0.1 seconds as before. A new state begins a crossfade from the last rendered base pose; interruption captures the current blend instead of returning to an old source. Physics remains authoritative and foot IK stays a later drawing adjustment.

Original procedural mannequin clips demonstrate all six states. Imported characters clear procedural clips and may select `run_clip`, `jump_clip`, `fall_clip`, `land_clip` through existing version-1 character-profile JSON. Missing run falls back to walk; missing airborne/landing clips fall back to idle. These are explicit compatibility fallbacks, not generated finished animation. Clip names must resolve uniquely in the imported asset. Unknown/invalid names fail before upload. Existing idle/walk selectors/profiles remain supported; no external asset dependency.

## Verification and limits

Check crossfade endpoints/shortest rotation, interruption continuity, alias/overlap, failure transactions and frame-rate partitioning. Test state hysteresis, apex, jump interruption, landing hold, initial-air/ground and invalid inputs. Verify procedural poses, imported clip fallback/selection/refusal and actual PlayerCharacter/Jolt state transitions. Native and hosted matrix required. Clip art/character feel, foot locks/orientation/body constraints, crowd budgets and target-PC acceptance remain open. This is a basic state/transition slice, not a complete locomotion graph or production movement system.
