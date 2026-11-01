# Working together on Lykoware

## Starting a session

1. Read [AGENTS.md](AGENTS.md) and [the work board](docs/WORK_BOARD.md).
2. Inspect the latest main branch and active issues/PRs. Reconcile any local changes before editing; never discard them automatically.
3. Choose one bounded task. Record its owner, base commit, scope, interfaces, and acceptance checks using the work-board template.
4. Use a separate branch such as feat/module-02-window or docs/engine-requirements. Use separate worktrees/checkouts when work runs concurrently.

The owner is whoever is actively implementing the task, whether human or assistant. Ownership is recorded coordination, not an automated lock. A missing issue does not prove nobody is working on an area.

## Making changes

Keep engine, tools, and game responsibilities separate. Before changing a shared public API or persisted format, document its proposed contract in an ADR and review it with affected contributors. The project owner resolves product scope decisions; contributors can choose routine implementation details within an agreed task.

C++20/CMake is accepted in [ADR 0001](docs/adr/0001-language.md); use the documented CMake presets. Follow [ownership/licensing policy](docs/adr/0000-ownership-and-licensing.md), keep [STATUS](docs/STATUS.md) current, and use docs/adr for new decisions.

For C++ changes, build and test affected behavior, then use the Windows CI checks to verify the primary target. Changes to builds, dependencies, packaging, or public headers should be exercised in Windows Debug and Release. Do not claim checks that have not finished.

## Review and integration

Open a focused PR with the problem, resulting behavior, relevant decisions, and validation evidence. Have another contributor review implementation and contract changes when available. Resolve failed checks and overlaps before merging. Merge only within the user's authorization.

Scoped documentation-only maintenance may go directly to main when the user requests saving shared plans/instructions. Recheck the remote head and preserve concurrent changes. This initial collaboration-document setup uses that path.

These are contributor conventions. Branch protection, mandatory approvals, and ownership locks are not configured by these documents. Repository access and tool permissions still govern each action.

## Finishing and handing off

Update the checklist only for completed, verified work. Update the work board when active focus changes. Leave a handoff in the PR/task using the shared template so the next assistant can continue without the chat.

For any assistant that does not automatically load repository instructions, start its session with:

> Read AGENTS.md and docs/WORK_BOARD.md in Avrenthrad/lykowareGE. Check the current branch, latest main, and active PRs before taking a task. Use the repository documents as shared context and report assumptions explicitly.
