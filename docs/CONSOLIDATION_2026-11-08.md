# Consolidation acceptance — 8 November 2026

PR [#65](https://github.com/Avrenthrad/lykowareGE/pull/65) merged as `aacff2525769fe8cd27ba0bd834c8d39c5b9f205`, preserving the dependency ancestry of every current PR head #37–64, including the independent notices/local-validation branch. Main was `7616ce3`; validated integration source was `13c097d924b19a366c448eab3cc9ddae7295ceef`. Joshua explicitly requested retrying validation and merging. This records technical review and authorized integration, not personal playtesting by Joshua.

## Completed checks

[Exact-head hosted run](https://github.com/Avrenthrad/lykowareGE/actions/runs/42386213427): all five jobs succeeded, including Windows Debug/Release, Linux Debug, ASan/UBSan and clang-tidy 18. Windows Release registered 70 tests: 49 passed, 21 Vulkan-dependent tests skipped because the hosted machine lacks Vulkan. Windows success includes staging, installed CLI execution and PowerShell measurement-script parsing. It does not establish physical GPU correctness.

An independent clean checkout passed 70/70 CTests in each Linux profile, with zero skips, plus configure/build/install/installed CLI. Report: `/workspace/lykoware-consolidation-final-13c097d/report.json`. Clean source fingerprint before/after: `f3c1dd2cbd0f1bc0f5357498b5b3bc48b06588782da366b4334e8aec615b5472`. GCC 14.2, CMake/CTest 3.31.6, clang-tidy 18, software Vulkan. Only the pinned host-tool cache was reused between checkouts; native trees originated fresh in the independent checkout and were incrementally updated for the four test-only conversion fixes. The earlier local run stopped during analysis and is not counted as complete. Reports/build products remain local and ignored.

Integration review resolved documentation conflicts, missing Lua notices in the notice-only install component, installed GPU-skinning shader coverage, and MSVC C4244 caused by integer literals passed through optional 16-bit joint indices in four fixtures. Explicitly typed indices fixed Windows without disabling warnings-as-errors. Upstream licence bytes were preserved; vendored whitespace was not rewritten. Earlier mixer/renderer ownership fixes and regressions are present in the integrated history; the frozen 6 November health report is historical. Proposed ADR status remains unchanged by merging implementation.

Stacked PRs whose heads are already ancestors of main are closed as incorporated by #65, with provenance added to their descriptions; original per-PR CI is not represented as independently passed. Contributor branches are retained. Unrelated #22 and the already-incorporated #30 are outside this consolidation's new work.

## Still open

- Target PC: owner-reported NVIDIA RTX 4070; exact VRAM and driver are unmeasured. Windows 11/i7-14700F/16 GB RAM were owner-supplied context, not queried here.
- Actual desktop input/focus/minimise/audio behavior and subjective character feel need target-PC execution.
- Presentation frame pacing, warmed hitches, RAM/VRAM residency and streaming/AI workloads need measured hardware traces. Software Vulkan and hosted Windows skips cannot substitute.
- Numeric frame/memory budgets remain proposals until calibrated on declared hardware. Existing CPU regression guards do not establish flagship 60 fps performance.
- Full Phase 0/1 acceptance, broad foot IK, later gameplay/editor/content phases and public-release review remain open.

## Target-PC procedure

From Developer PowerShell with Visual Studio/CMake/Python prerequisites installed:

```powershell
python tools/validation/verify.py
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits
powershell -File tools/validation/measure-scene.ps1
```

Keep the GPU inventory output with the local evidence (memory is MiB). If NVIDIA tooling is unavailable, leave VRAM unconfirmed; do not use the 32-bit WMI AdapterRAM field as authoritative. The measurement runner deliberately reports only a small offscreen fixture. Review its scope before making budget claims. No automatic upload, server access or paid service is required. After collecting that evidence, add an actual windowed presentation/streaming run and agree enforceable budgets before closing Phase 1.
