# Local native validation

Run `tools/validation/verify.py` with Python 3.10+ from the repository. Install the platform prerequisites in [BUILDING.md](BUILDING.md) first. This command gathers local build/test/install evidence; it does not replace required hosted CI or authorize a merge.

On Windows, use Developer PowerShell for Visual Studio:

```powershell
python tools/validation/verify.py
```

This runs both Windows Debug and Release by default. The Windows execution path has orchestration tests, but actual execution on Windows remains pending.

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
