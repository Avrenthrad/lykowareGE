# Local native validation

Run `tools/validation/verify.py` with Python 3.10+ from the repository. Install the platform prerequisites in [BUILDING.md](BUILDING.md) first. This command gathers local build/test/install evidence; it does not replace required hosted CI or authorize a merge.

On Windows, use Developer PowerShell for Visual Studio:

```powershell
python tools/validation/verify.py
```

This runs both Windows Debug and Release by default. The Windows execution path has orchestration tests; hosted Windows Debug/Release build/test/install/staged execution passed in consolidation run 42386213427. Running this validator on the owner’s target PC and hardware measurements remain pending.

On Linux, the default is Debug. For the complete Linux matrix:

```sh
python3 tools/validation/verify.py --preset linux-debug --preset linux-sanitizers --preset linux-analysis --clang-tidy /path/to/clang-tidy-18
```

Clang-tidy must be major version 18. Analysis does not itself change the C++ compiler. `--jobs 2` is the default; accepted range is 1–64. Tool paths can be supplied with `--cmake`, `--ctest` and `--git`. `--source /path/to/checkout` supports invoking a copied validator outside the checkout. The source must be a Git repository root containing the presets. `--timeout-seconds` bounds each command (default 7200).

## Evidence and interpretation

Each invocation uses a fresh output directory: by default ignored `build/validation/<UTC-time>-<unique-id>`. `--output /path/to/new-directory` selects another fresh directory. Existing output is rejected; an output inside the checkout must be gitignored. Build trees follow the repository presets. Do not run concurrent validations against the same checkout or edit inputs while verification runs.

The runner records source commit, initial dirtiness, SHA-256 input fingerprint before/after, tool/compiler versions, configured verification flags, stage logs and individual test results in `report.json`. Configure, build, test discovery, CTest, install and installed CLI execution are distinct stages. Tests and warnings are enabled explicitly. Empty/disabled/all-skipped suites, bad or mismatched JUnit, failed commands and changed source fail verification. JUnit hostname attributes are removed from saved results.

- `passed`: every selected stage/test passed without skips; exit 0.
- `passed_with_skips`: the completed run has skipped tests; exit 0, with limitations recorded. A skipped GPU test is not graphics verification.
- `failed`: a stage, test or integrity check failed; exit 1.
- `interrupted`: interrupted execution, with evidence retained; exit 130.
- Invalid command usage exits 2 before validation starts.

A dirty starting tree is allowed and explicitly recorded: the result applies to fingerprinted worktree contents, not only the named commit. Fingerprints cover tracked and unignored repository inputs, not external SDKs/drivers or ignored caches. They cannot detect a change reverted during execution. This is not a hermetic or tamper-proof attestation.

Reports stay local; there is no upload or billing/account modification. The runner avoids querying account tokens, environment dumps or personal machine identifiers, but native logs can contain local paths and test stdout. Review logs before sharing them. Generated reports, captures and build trees must not be committed.

The runner's failure-path checks are in `tests/validation_runner.py`, registered as `validation.runner`. The [6 November health check](HEALTH_CHECK_2026-11-06.md) records real main and combined-stack executions, scope limitations and two additional reproduced defects.

## RTX 4070 evidence and assessment

On the target Windows PC, run:

```powershell
./tools/validation/measure-scene.ps1
```

Each invocation creates a fresh folder under `build/measurements/`; an existing explicit `-OutputDirectory` is refused. Version 2 hardware metadata records source commit/dirtiness, executable SHA-256, fixture hashes/configuration and available NVIDIA adapter names, total memory in MiB and driver versions. `nvidia-smi` is optional; absent/invalid inventory stays unknown. No hostname, username or GPU UUID is collected. Inventory does not by itself identify the adapter selected by Vulkan. Do not use WMI AdapterRAM for VRAM size.

After three runs, `assess_scene.py` writes `assessment.json`: it recomputes mean/nearest-rank p99/max from raw durations and checks device/replay/final-state agreement, resolution, warmup, ticks, sample counts and physical rendering. Peak process RAM is included. A missing/unmatched/ambiguous NVIDIA inventory is `incomplete`; malformed/inconsistent reports or overruns are `failed`. Only `collected` exits zero. A completed report **always leaves Phase 1 and recommended-profile acceptance false**. Reports are local diagnostic evidence, not authenticated attestations; a custom executable hash does not prove it was built from the recorded commit.

Reassess saved evidence without rerunning the engine:

```powershell
python tools/validation/assess_scene.py build/measurements/YOUR-RUN
```

Optional reviewed budgets use this schema; copy the **complete** `fixture` object from that run's `hardware.json` (including its hashes and run configuration):

```json
{
  "format": "lykoware.offscreen-budget",
  "schema_version": 1,
  "status": "provisional",
  "fixture": {},
  "limits": {"p99_ms": 25.0}
}
```

The empty fixture above is deliberately invalid until filled. `limits` accepts `mean_ms`, `p99_ms`, `max_ms` and `peak_process_resident_bytes`, all finite positive values; choose them from reviewed evidence. The 25 ms example illustrates the proposed 60 Hz p99 direction, not an agreed/calibrated limit. No budget is supplied by default. Invoke with `--budget FILE`, or pass `-BudgetFile FILE` to the PowerShell runner. Any run exceeding any supplied limit fails; equality passes. Limits apply solely to this small synchronous offscreen fixture. Existing scene summaries use a different percentile convention; the assessor explicitly uses nearest rank from raw samples.

This does not measure displayed frame pacing, GPU timestamp time, VRAM residency, audio behavior, streaming or local AI. Target-PC desktop tests and representative measured budgets are still required before closing Phase 1.
