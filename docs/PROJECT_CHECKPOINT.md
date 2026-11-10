# Lykoware — integrated animation stack checkpoint

On 10 November 2026 the owner asked Claude to review #71–#74 and merge them if they looked and tested good. They did. #71, #72 and #73 were merged in dependency order, and this documentation (#74) after them.

## Main baseline

Before #74, `main` is **6cabcf34b215b9a360ccfc7359cbde916a4ada2e**, the merge of #73. It builds on #70 (`9555fea`, terrain turning and controller authority).

Integrated foundation includes C++20/CMake, Windows-first Vulkan, Jolt/ECS, input/cameras, rendering/assets/audio/scripting, replay/capture/validation tools, skeletal skinning/IK, imported profiles/alignment, six-state sample locomotion, authored animation graphs with events, sockets and retargeting, stationary foot orientation with bounded pelvis compensation, and walking foot anchors with deterministic player replay. The selected 15-item batch is **15/15 integrated**. Broad Phase 1 hardware and Phase 2 feel/performance acceptance is still open; this is not a finished engine or editor.

## Integrated feature stack

| Order | PR / branch | Exact head merged | Merged as | Scope |
|---|---|---|---|---|
| 1 | [#71](https://github.com/Avrenthrad/lykowareGE/pull/71), `codex/authored-animation-runtime` | `71975870bd55b3b0e1aa4af2572ef0c038816aca` | `35ad909` | Authored animation graphs, clip events, joint sockets, supported-rig retargeting, `lyko animate-character`. ADR 0065. |
| 2 | [#72](https://github.com/Avrenthrad/lykowareGE/pull/72), `codex/terrain-foot-constraints` | `e21f797973d72b0bef5e67127eebfe4ab929057e` | `58e170e` | Stationary surface-normal foot orientation and bounded visual pelvis correction; ray hits report surface normals. ADR 0066. |
| 3 | [#73](https://github.com/Avrenthrad/lykowareGE/pull/73), `codex/walking-foot-anchors` | `36460d88099936aabcea485837ce518ba38ba78c` | `6cabcf3` | Walking support-local foot anchors, authored plant/lift cycles, bounded reach release, strict `--play-input` replay and `lyko run --` forwarding. ADR 0067. |

#72 and #73 were retargeted to `main` after their base PR merged, so each merge carried only its own changes. ADRs 0065–0067 remain Proposed; merging did not accept them. Branches were kept; nothing was force-pushed or deleted.

## Review and re-verification before the merge (Claude, 10 November 2026)

- **Hosted CI:** every exact head had passed all five checks (Windows Debug/Release, Linux Debug, ASan/UBSan, clang-tidy 18): runs 42481306674 (#71), 42485289763 (#72), 42544150921 (#73) and 42544153487 (#74).
- **Local re-verification:** `main` (`9555fea`) with #71, #72 and #73 merged in order passed **82/82 in each of Debug, ASan/UBSan and clang-tidy 18**, with no failures and no build warnings. That tree is identical to the merged `main` at `6cabcf3`.
- **Code review:** no blocking issues.
  - **Animation graph:** steps are transactional, events fire once per crossing including across loop wraps, edges are taken in authored order, and inputs are bounded.
  - **Retargeting:** the rotation transfer is correct for the supported subset; `compare_rigs` requires a full name mapping, so no target joint is left unposed.
  - **Joint-axis solver:** it checks the rendered matrix before committing.
  - **Ray casts:** they read surface normals under a body lock without changing physics.
  - **Foot plants:** they stage changes on a copy and release on stale or foreign support handles.
  - **Replay parser:** it is bounded (64 KiB, 6,000 frames, finite inputs).
  - **`lyko run --`:** it forwards arguments without a shell.
  - **Nits, not fixed:** `GraphPlayer::create` replaces the underlying skeleton or clip error with a generic message; `Retarget::apply` and the IK scratch use about 26–30 KiB of stack or caller storage per call, as documented.
- **#74:** it conflicted with #73's handoff paragraphs in STATUS, WORK_BOARD and MASTER_CHECKLIST. The conflicts were resolved by replacing both "parked" snapshots with the merged state. 02.08–02.10, 02.11c-normals, 02.11c-locks and 02.11d-pelvis are now checked, because their only remaining gate was the merge.

## Verification evidence recorded before the merge (Codex)

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
- Walking foot-heading locks, support render interpolation, full body balance and ragdolls are future/unfinished work, not completion claims for #72 or #73.
- Public/Pax/legal and any paid-service authorization remain later gates.

## Safe continuation point

1. The #71 → #72 → #73 stack is integrated. Further merges to `main` still need the owner's say-so.
2. Read this checkpoint, AGENTS, STATUS and WORK_BOARD before editing; older pause and WIP paragraphs describe historical commits.
3. Resume the next roadmap module only as a separate scoped task. Walking foot heading, render interpolation, one-foot balance, full body constraints and ragdolls are still future work.
4. Collect physical Windows/RTX 4070 measurements and the owner's play review when the PC is available. Software and hosted graphics checks cannot close those gates.

Ignored build outputs under the primary checkout may be from the selected feature tree (currently walking-anchor runtime), even after checkout returns to main. They are preserved generated artifacts, not main verification. Reconfigure/rebuild the selected source before running them; do not interpret a stale executable as the main engine. Source commits, CI results and external validator reports are the recorded evidence.
