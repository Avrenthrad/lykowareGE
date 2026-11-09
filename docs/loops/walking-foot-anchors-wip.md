# Walking foot anchors — saved WIP history and resumed acceptance

The owner subsequently requested completion of the listed PR work. Implementation resumed on #73; see [walking runtime](../WALKING_FOOT_PLANTS.md) and [progress](engine-progress.md) for current behavior/checks. The original pause below describes only84d036b and must not override later verified evidence. Resumed source is now reviewed and verified; the final evidence is recorded in STATUS and loop progress. The original saved WIP is not current acceptance evidence.

## Historical pause checkpoint

Owner requested pausing development, committing all outstanding work and returning the primary checkout to main because of usage/connection conflicts. This file checkpoints the partial implementation without implying it is ready for integration.

Branch: `codex/walking-foot-anchors`. Base: #72 corrected runtime `e21f797`. Dependency order: #71 → #72 → this branch. ADR0067 is Proposed. Main has none of these feature branches integrated. Preserve all branches; do not merge/main-push without a later instruction.

## Implemented in the working checkpoint

- New owned fixed-storage `animation::FootPlant` policy/state/result API and implementation: real collision acquisition, support-body-local point/normal, transported anchors, lift/release/suppression and failure staging.
- CMake source registration.
- Character-profile `foot_locking` / `foot_max_plant_distance`, dependency checks and plant/lift graph-marker inspection.
- Playback phase tracking for `left_plant`, `left_lift`, `right_plant`, `right_lift`, including state/reset handling.
- Partial foot-controller integration: staged anchor states, planted versus swing targets, bounded reach-release retries and pelvis recomputation.
- Partial PlayerBody moving/grounded/visible phase integration, debug counters and inspection JSON fields.
- Existing inspection-test expected default policy was extended; no new behavior tests have been added yet.

## Unfinished / unverified

This entire walking-foot checkpoint has **not been compiled, executed, independently reviewed or run through CI**. It may contain integration or syntax issues. Do not check off walking locks or claim zero allocations/correct support motion from this checkpoint. Assertions in ADR0067 describe intended acceptance, not completed evidence.

Next actions after the owner chooses to resume:

1. Confirm #72 is the intended dependency and compare branch bases before editing; preserve its independent FK fix.
2. Compile this source, inspect Controller retry/commit control flow and shared profile-policy plumbing; fix any ordinary integration errors.
3. Add real Jolt static/kinematic translation+rotation, far-origin, destroyed/reused/cross-world support, phase/release/distance/slope/invalid-input/ownership/allocation tests for FootPlant.
4. Test authored time-zero/loop/state/reset plant/lift markers through actual Playback. Verify walking body movement versus fixed foot anchors and material reach-release with full bilateral rollback.
5. Extend original imported-profile/sandbox/GPU fixtures; inspect captures. Do not replace runtime behavior with test doubles.
6. Independent review, clean build checkpoint, full native Debug/sanitizers/analysis and exact-head Windows/Linux CI.
7. Update the checklist only with verified scope. Physical-PC feel/pacing, foot-heading locks and support render interpolation remain separate gates.

Related source files: engine/include/lykoware/foot_plant.hpp, engine/src/foot_plant.cpp, engine/CMakeLists.txt, character_asset.hpp/.cpp, sample Playback/foot_placement/PlayerBody, character inspection CLI/test and ADR0067. The local uncommitted source was saved as-is, not advertised as a finished module.
