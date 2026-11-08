# Lykoware — owner-requested pause checkpoint

Development is paused for owner evaluation following usage/connection conflicts. All previously uncommitted engine source has been committed and pushed. The primary checkout returns to `main`; this checkpoint is documentation only and is kept on `docs/project-pause-checkpoint` for review. No feature PR has been merged and no main push has been performed.

## Main baseline

`main` / `origin/main`: **9555fea3c5fe57d0663bbc92f31cedee5c6bb989**, merged PR #70 (terrain turning and explicit controller authority). The local main branch was behind by 200 commits; it was fast-forwarded to the existing remote main without creating a merge commit.

Integrated foundation includes C++20/CMake, Windows-first Vulkan, Jolt/ECS, input/cameras, rendering/assets/audio/scripting, replay/capture/validation tools, skeletal skinning/IK, imported profiles/alignment and six-state sample locomotion. The selected 15-item batch has **12 integrated requirements**. The final three are implemented/verified in #71 but remain unmerged. Broad Phase1 hardware and Phase2 feel/performance acceptance is still open; this is not a finished engine or editor.

## Not integrated into main — active feature stack

| Order | PR / branch | Exact source checkpoint | Actual status | Remaining action |
|---|---|---|---|---|
| 1 | [#71](https://github.com/Avrenthrad/lykowareGE/pull/71), `codex/authored-animation-runtime` | `71975870bd55b3b0e1aa4af2572ef0c038816aca` | Authored animation graphs, events/sockets, supported-rig retargeting. Independently reviewed; all five hosted profiles passed; local 79/79 each Linux profile, zero skips. | Owner chooses integration; then preserve this dependency before #72. |
| 2 | [#72](https://github.com/Avrenthrad/lykowareGE/pull/72), `codex/terrain-foot-constraints` | `e21f797973d72b0bef5e67127eebfe4ab929057e` | Stationary surface-normal foot orientation and bounded visual pelvis correction. Independent review fix verified; clean build, all five hosted profiles and local 81/81 each Linux profile passed, zero local skips. | Owner chooses integration after #71. Walking locks are not included in this verified scope. |
| 3 | [#73](https://github.com/Avrenthrad/lykowareGE/pull/73), `codex/walking-foot-anchors` | `84d036bfaa679d88316e3bf055a48e8838e3a6e7` | **UNVERIFIED WIP**, now committed/pushed. New FootPlant anchor API, profile/graph phases and partial walking controller/PlayerBody integration. No local compile, execution, behavior tests or review has been completed. | Inspect/fix integration; add real support-motion, phase, release, transaction/allocation and sandbox/GPU tests; review and run full matrix. Do not merge as finished work. |

Bases are intentional: #71 → main; #72 → #71 branch; #73 → #72 branch. Do not merge #72/#73 out of order, replay the same source twice or force-push shared branches. A separate documentation-only pause-checkpoint PR records this status and remains unmerged under the same standing restriction. All source checkpoints are recoverable from GitHub, not dependent on the chat or a stash.

WIP details are saved with #73 in `docs/loops/walking-foot-anchors-wip.md` and Proposed ADR0067. Stance details are in #72's `docs/TERRAIN_STANCE.md` / ADR0066. Existing Proposed ADRs do not become Accepted just because implementation/CI exists.

## Verification evidence

- #71 final-head [CI42481306674](https://github.com/Avrenthrad/lykowareGE/actions/runs/42481306674): Windows Debug/Release and Linux Debug/sanitizers/analysis all passed. Local report `/workspace/lykoware-animation-handoff-validation/report.json`, unchanged clean source `7197587`, fingerprint `ae6a0b69e150eae847c6915c7ee91ea53cf7bcc40f3c7c70cb26467c4aa062a6`; 79/79 each native profile with install/staged CLI.
- #72 exact-head [CI42485289763](https://github.com/Avrenthrad/lykowareGE/actions/runs/42485289763): the same five profiles all passed. Local report `/workspace/lykoware-terrain-stance-reviewed-validation/report.json`, unchanged clean source `e21f797`, fingerprint `2e00eff2640b1e0a64858fdbdb2b76540019220aee7438ee0eb8c792b994a529`; 81/81 each native profile with install/staged CLI. Clean build and real imported/GPU-versus-CPU stance capture passed; capture was visually inspected.
- Reviewer found a real FK-axis mismatch for a near-unit quaternion parent. Source `e21f797` includes actual rendered-matrix verification and transactional regressions. Reviewed duplicate `967a17b` has the identical tree; no blocking review finding remains for #72.
- Initial stance report `/workspace/lykoware-terrain-stance-validation-v1/report.json` was superseded/interrupted and is **not** full corrected-source acceptance evidence.
- Hosted Windows graphics checks have GPU-related skips. Local software Vulkan checks do not prove physical RTX4070 behavior or budgets.
- #73 has no completed feature-verification evidence. Any automatically triggered draft CI can only show existing coverage; missing walking-anchor acceptance tests still have to be written/executed.

## Other branches and open PRs requiring awareness

| Ref | Status / action |
|---|---|
| `codex/stance-review-fixes` at `967a17b` | Local review-copy commit; its entire tree is identical to #72 `e21f797`. Preserved for traceability; no distinct feature to merge. |
| `codex/health-full-stack` at `48b0da9` | Historical integration-test merge node, not an ancestor of main. Both parents are in main. It contains merge-resolution edits (CMake/docs/vendor notes), so preserve/audit before archival; do not blindly merge the old stack. |
| `codex/validate-package-stack` at `15c0b33` | Historical integration-test merge node with both parents in main and remaining merge-resolution metadata. Preserve/audit before archival. |
| [#30](https://github.com/Avrenthrad/lykowareGE/pull/30), `claude/nice-mendel-lm4mb0` | Historical open PR based on `claude/ecs-core`. Current main already retires physics slots at final generation; the outstanding head is a merge/docs node, not separate new runtime implementation. Reconcile the PR record before closing/archive; no change made here. |
| [#22](https://github.com/Avrenthrad/lykowareGE/pull/22), `cursor/lykoware-remaster-docs-567c` | Separate unmerged Remaster documentation proposal, outside the current runtime work. Owner scope/legal/editor sequencing decision remains separate. |

All other local branch tips were found to be ancestors of current main during this checkpoint. No branches were deleted, rebased or force-pushed. The temporary walking implementation worktree was removed only after its complete source was committed/pushed and its tracked/untracked working-tree status was clean.

## Manual / owner matters parked

- Actual RTX4070 inventory: exact VRAM/driver still unmeasured.
- Windows physical-PC startup/input/render behavior, pacing/hitches and RAM/VRAM measurements; calibrated performance budgets.
- Owner character-feel/play review and production-asset acceptance.
- Walking foot-heading locks, support render interpolation, full body balance and ragdolls are future/unfinished work, not completion claims for #72.
- Public/Pax/legal and any paid-service authorization remain later gates.

## Safe continuation point

1. Owner evaluates this status; development remains paused.
2. Decide whether to integrate the verified #71/#72 stack or continue fixing #73 first. The standing no-merge/main-push restriction remains until explicitly changed.
3. Confirm the chosen branch/base and the latest remote state before editing. Read its AGENTS, STATUS, WORK_BOARD and loop/WIP notes. Do not use older “active” paragraphs as authority over this checkpoint.
4. If resuming #73, start with its WIP note, compile source and add missing behavior tests; do not start a new module or mark walking locks complete from a green baseline suite.

Ignored build outputs under the primary checkout may still be from feature source `e21f797`, even after checkout returns to main. They are preserved generated artifacts, not main verification. Reconfigure/rebuild the selected source before running them; do not interpret a stale executable as the main engine. Source commits, CI results and external validator reports are the recorded evidence.
