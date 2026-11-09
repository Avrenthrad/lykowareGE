# Lykoware — verified runtime stack checkpoint

The owner resumed work to complete the listed PR tasks. Walking anchors now have real runtime coverage and the independent review finding has been fixed. Clean native validation, all five hosted profiles and independent review have passed. This documentation-only branch remains `docs/project-pause-checkpoint`; no feature PR is merged and no main push is performed.

## Main baseline

`main` / `origin/main`: **9555fea3c5fe57d0663bbc92f31cedee5c6bb989**, merged PR #70 (terrain turning and explicit controller authority). The local main branch was behind by 200 commits; it was fast-forwarded to the existing remote main without creating a merge commit.

Integrated foundation includes C++20/CMake, Windows-first Vulkan, Jolt/ECS, input/cameras, rendering/assets/audio/scripting, replay/capture/validation tools, skeletal skinning/IK, imported profiles/alignment and six-state sample locomotion. The selected 15-item batch has **12 integrated requirements**. The final three are implemented/verified in #71 but remain unmerged. Broad Phase1 hardware and Phase2 feel/performance acceptance is still open; this is not a finished engine or editor.

## Not integrated into main — active feature stack

| Order | PR / branch | Exact source checkpoint | Actual status | Remaining action |
|---|---|---|---|---|
| 1 | [#71](https://github.com/Avrenthrad/lykowareGE/pull/71), `codex/authored-animation-runtime` | `71975870bd55b3b0e1aa4af2572ef0c038816aca` | Authored animation graphs, events/sockets, supported-rig retargeting. Independently reviewed; all five hosted profiles passed; local 79/79 each Linux profile, zero skips. | Owner chooses integration; then preserve this dependency before #72. |
| 2 | [#72](https://github.com/Avrenthrad/lykowareGE/pull/72), `codex/terrain-foot-constraints` | `e21f797973d72b0bef5e67127eebfe4ab929057e` | Stationary surface-normal foot orientation and bounded visual pelvis correction. Independent review fix verified; clean build, all five hosted profiles and local 81/81 each Linux profile passed, zero local skips. | Owner chooses integration after #71. Walking locks are not included in this verified scope. |
| 3 | [#73](https://github.com/Avrenthrad/lykowareGE/pull/73), `codex/walking-foot-anchors` | `b7dbd15319fe7e292bb0c86abaccaee66a754d52` | Walking support-local anchors, authored cycle phases, bounded release, strict actual player replay and CLI forwarding. Independent reviewer rechecked the loop-boundary fix with no blocking findings. | Verified; integration remains parked after #72. |

Bases are intentional: #71 → main; #72 → #71 branch; #73 → #72 branch. Do not merge #72/#73 out of order, replay the same source twice or force-push shared branches. Documentation-only PR #74 records this status and remains unmerged under the same standing restriction. All source checkpoints are recoverable from GitHub, not dependent on the chat or a stash.

The original84d036b WIP record is historical. Current #73 behavior is in `docs/WALKING_FOOT_PLANTS.md` and Proposed ADR0067; its loop progress records actual commands and review corrections. Stance details are in #72's `docs/TERRAIN_STANCE.md` / ADR0066. Existing Proposed ADRs do not become Accepted just because implementation/CI exists.

## Verification evidence

- #71 final-head [CI42481306674](https://github.com/Avrenthrad/lykowareGE/actions/runs/42481306674): Windows Debug/Release and Linux Debug/sanitizers/analysis all passed. Local report `/workspace/lykoware-animation-handoff-validation/report.json`, unchanged clean source `7197587`, fingerprint `ae6a0b69e150eae847c6915c7ee91ea53cf7bcc40f3c7c70cb26467c4aa062a6`; 79/79 each native profile with install/staged CLI.
- #72 exact-head [CI42485289763](https://github.com/Avrenthrad/lykowareGE/actions/runs/42485289763): the same five profiles all passed. Local report `/workspace/lykoware-terrain-stance-reviewed-validation/report.json`, unchanged clean source `e21f797`, fingerprint `2e00eff2640b1e0a64858fdbdb2b76540019220aee7438ee0eb8c792b994a529`; 81/81 each native profile with install/staged CLI. Clean build and real imported/GPU-versus-CPU stance capture passed; capture was visually inspected.
- Reviewer found a real FK-axis mismatch for a near-unit quaternion parent. Source `e21f797` includes actual rendered-matrix verification and transactional regressions. Reviewed duplicate `967a17b` has the identical tree; no blocking review finding remains for #72.
- Initial stance report `/workspace/lykoware-terrain-stance-validation-v1/report.json` was superseded/interrupted and is **not** full corrected-source acceptance evidence.
- Hosted Windows graphics checks have GPU-related skips. Local software Vulkan checks do not prove physical RTX4070 behavior or budgets.
- #73: Verified runtime source `b7dbd15319fe7e292bb0c86abaccaee66a754d52`, tree `335ef411e82468b985efc854d1857e97d1d82381`. [Hosted CI42542802775](https://github.com/Avrenthrad/lykowareGE/actions/runs/42542802775) passed Windows Debug/Release and all three Linux profiles. Clean Debug rebuild passed all561 steps; local Debug, ASan/UBSan and clang-tidy18 each passed82/82, zero failures/skips, including configure/build/install/staged CLI. Report `/workspace/lykoware-foot-plants-reviewed-validation/report.json`, unchanged clean-source fingerprint `36d8d05922141fa3fd4693406b2aba866423a6160ff678efc7c7a4b50f6dfe5c`. The native report names local review commit `ba473cba7fc167afd644008e379cd7bcea4b9bb3`; its entire Git tree equals published `b7dbd153`, with only commit metadata different. Independent reviewer rechecked the loop-boundary correction and found no blocking issues. Hosted Windows has21 graphics skips per pass; physical RTX4070 acceptance is still unverified. Its new coverage exercises real static/translating/rotating/tilting supports, stale/cross-world/lifetime guards, far-origin precision, hidden lift/replant and wrap ordering, bilateral rollback, reach release and zero-allocation controls. Actual moving imported player replay is repeated directly and through `lyko`; GPU deformation matches CPU reference and the capture was visually inspected.

## Other branches and open PRs requiring awareness

| Ref | Status / action |
|---|---|
| `codex/stance-review-fixes` at `967a17b` | Local review-copy commit; its entire tree is identical to #72 `e21f797`. Preserved for traceability; no distinct feature to merge. |
| `codex/health-full-stack` at `48b0da9` | Historical integration-test merge node, not an ancestor of main. Both parents are in main. It contains merge-resolution edits (CMake/docs/vendor notes), so preserve/audit before archival; do not blindly merge the old stack. |
| `codex/validate-package-stack` at `15c0b33` | Historical integration-test merge node with both parents in main and remaining merge-resolution metadata. Preserve/audit before archival. |
| [#30](https://github.com/Avrenthrad/lykowareGE/pull/30), `claude/nice-mendel-lm4mb0` | Historical open PR based on `claude/ecs-core`. Current main already retires physics slots at final generation; the outstanding head is a merge/docs node, not separate new runtime implementation. Reconcile the PR record before closing/archive; no change made here. |
| [#22](https://github.com/Avrenthrad/lykowareGE/pull/22), `cursor/lykoware-remaster-docs-567c` | Separate unmerged Remaster documentation proposal, outside the current runtime work. Owner scope/legal/editor sequencing decision remains separate. |

Other local branch tips were found to be ancestors of main at the original pause checkpoint. A new local boundary-review commit is retained separately when synchronizing its identical tree to the connector-published commit. No branches were deleted, rebased or force-pushed. The temporary walking implementation worktree was removed only after its complete source was committed/pushed and its tracked/untracked working-tree status was clean.

## Manual / owner matters parked

- Actual RTX4070 inventory: exact VRAM/driver still unmeasured.
- Windows physical-PC startup/input/render behavior, pacing/hitches and RAM/VRAM measurements; calibrated performance budgets.
- Owner character-feel/play review and production-asset acceptance.
- Walking foot-heading locks, support render interpolation, full body balance and ragdolls are future/unfinished work, not completion claims for #72.
- Public/Pax/legal and any paid-service authorization remain later gates.

## Safe continuation point

1. Preserve #71 → #72 → #73 dependency order; all remain unmerged under the standing owner restriction.
2. Read this checkpoint, the selected branch's AGENTS/STATUS/WORK_BOARD and current loop evidence before editing; older pause/WIP paragraphs describe historical commits.
3. Resume the next roadmap module only as a separate scoped task. Walking foot heading, render interpolation, one-foot balance, full body constraints and ragdolls are still future work.
4. Collect physical Windows/RTX4070 measurements and owner play review when the PC is available. Software/hosted graphics checks cannot close those gates.

Ignored build outputs under the primary checkout may be from the selected feature tree (currently walking-anchor runtime), even after checkout returns to main. They are preserved generated artifacts, not main verification. Reconfigure/rebuild the selected source before running them; do not interpret a stale executable as the main engine. Source commits, CI results and external validator reports are the recorded evidence.
