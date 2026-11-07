# ADR 0046: Local build and test evidence

Status: Proposed. Implements the existing native verification workflow; no runtime API, CI policy or new dependency.

GitHub currently refuses jobs before runners start. Add a local Python 3.10+ command that runs the existing CMake/CTest presets and stages the resulting programs. Windows defaults to Debug and Release; Linux defaults to Debug, with explicit sanitizer/analysis selections. Commands use argument arrays, never a shell. Default build concurrency is two jobs.

Each invocation creates a fresh report directory under ignored build output (or outside the source tree), saves tool logs, and writes a schema-versioned JSON report. Record the commit, worktree dirtiness, a SHA-256 fingerprint of tracked/unignored repository inputs before and after the run, actual tool/compiler details, each stage's result and the individual passed/skipped/failed tests. Do not collect environment variables, account tokens, hostname or username. No upload, account configuration, paid operation, runner registration or CI status write occurs.

Configure explicitly enables tests and warning gates and selects the requested sanitizer/analysis flags. Discover tests before executing CTest; refuse empty or disabled suites. Parse a fresh JUnit result and require its names to match discovery exactly. Failed commands/tests, absent or malformed results, all-skipped suites, and changed source fail verification. A successful run with some skips is labelled `passed_with_skips`, never a full graphics/hardware pass. Staging and the installed CLI version check are separate recorded steps.

Source fingerprints cover repository input bytes and symlink targets, excluding ignored build products; they are provenance aids, not a hermetic-build or tamper-proof attestation. External SDKs/drivers and changes reverted during a build are not certified. Do not edit source during a verification run. Local results do not make blocked hosted checks green or authorize a merge. Owner review, Windows execution, hardware budgets and release gates remain separate.

Tests must cover genuine and misleading success paths: zero/disabled/all-skipped tests, mismatched JUnit cases, build/test/install failure, timeout/interruption, stale output directories, changed source and paths containing spaces/Unicode. Actual local execution validates the complete command flow; Windows/MSVC remains pending until run on Windows.

Implementation evidence: draft #47; 11 runner tests cover failure paths and explicit-source relocation. Exact main passed 41 Linux Debug CTests; combined snapshot `48b0da9` passed 55 each in Debug, ASan/UBSan and clang-tidy 18, no skips, unchanged source. See [guide](../LOCAL_VALIDATION.md) and [health evaluation](../HEALTH_CHECK_2026-11-06.md). Windows execution and required hosted checks remain pending. Additional health probes found two pending-module ownership defects; passing validation is not complete correctness proof.
