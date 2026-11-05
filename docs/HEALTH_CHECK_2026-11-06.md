# Engine health check — 6 November 2026

The Linux foundation builds and runs successfully. The pending stack is not yet merge-ready: additional ownership probes reproduced two correctness defects, and fresh Windows/hardware validation is missing. Phase 1 remains open.

## Frozen source and results

| Tree | Commit | Profile | CTests | Skipped |
| --- | --- | --- | --- | --- |
| Merged main | `7616ce3c07374d47924e95a6a5f87856ded96148` | Linux Debug | 41/41 passed | 0 |
| Combined development stack | `48b0da9d52bef2b183f92ecc911a5f0620214d21` | Linux Debug | 55/55 passed | 0 |
| Same combined tree | same | Linux ASan/UBSan | 55/55 passed | 0 |
| Same combined tree | same | Linux clang-tidy 18 | 55/55 passed | 0 |

The combined snapshot is branch `codex/health-full-stack`: local-validation source `6bfc2a9740b98d056084729d7a75dced445c30f8`, package notices #45, and Claude's #37–#44/#46 through `67cce0540488e1d38a1dcdb31109c8333e20717d`. It is an integration artifact, not a merge into main. No Claude branch was changed.

All configure, build, discovery, test, install and installed-CLI steps passed. Both snapshots were clean and unchanged throughout validation. Input SHA-256 fingerprints:

- Main: `9e4c74976aec43e059eb48de1f01fbc8976f990b3b6f2bd6ef24b138449b81d6`.
- Combined: `dc9811b440268a2faf08f571876a5b04c46dbf422d4287ef5fda1eec6b0ecb06`.

Environment: CMake/CTest 3.31.6, GCC 14.2.0, clang-tidy 18 for analysis, Mesa llvmpipe/lavapipe LLVM 19 software Vulkan, three hardware threads. The analysis profile still compiles with GCC. These are Linux results, not Windows or physical GPU results.

A real installed-CLI capture rendered the falling-crates scene at 960×540 after 120 ticks: four entities, final state hash `17658475507564817576`. The image contains the floor and coloured cubes. Capture reports `hardware_gpu=false`.

## Reproduced findings

### P2: voice handles cross mixer ownership boundaries

[Pending audio implementation](https://github.com/Avrenthrad/lykowareGE/blob/67cce0540488e1d38a1dcdb31109c8333e20717d/engine/src/audio.cpp#L239) checks slot index and generation but no mixer identity. Two independent mixers return equal first voice IDs. Passing A's ID to B stops B's unrelated voice.

Actual probe output:

```text
equal_ids=1 b_accepts_a_id=1 b_stop_a_id=1 b_own_voice_stopped=1
```

[Standalone reproducer](health/repros/audio_owner.cpp). Add per-mixer ownership identity with documented lifetime/copy/move semantics, following ADR 0030's ownership principle. Cover independent mixers, output instances and destroy/recreate cycles. This defect is in pending audio work, not merged main.

### P2: old renderer IDs alias replacement resources

[Pending renderer implementation](https://github.com/Avrenthrad/lykowareGE/blob/67cce0540488e1d38a1dcdb31109c8333e20717d/engine/src/render.cpp#L101) resolves mesh/texture IDs by array position. Destroy renderer A, create B, upload a different texture: A's retained IDs are accepted by B, including in mesh-part construction. A real draw uses B's blue texture instead of rejecting the old red texture ID.

Actual probe output:

```text
centre_rgb=0,0,92
mesh_ids_equal=1 texture_ids_equal=1 accepts_old_texture_in_mesh=1 draws_old_renderer_ids=1
```

[Standalone reproducer](health/repros/render_owner.cpp). Add renderer ownership identity and reject foreign/stale IDs at upload and draw boundaries. Test separate creators and destroy/recreate. The probe creates renderers sequentially, respecting the documented one-renderer-per-colour-format restriction. This defect affects pending mesh/texture APIs.

Both probes exit 1 when the reproduced bug occurs, 2 on setup failure, and 0 when the probed bad behavior is absent. They are manual diagnostic programs, not added to the passing CTest suite. A future fix needs full regression tests, not only these probes.

## Performance and verification limits

Debug VM observations: the 1,024-body/300-tick physics test averaged 16.001 ms, p95 62.862 ms, maximum 88.044 ms. ECS iteration over 100,000 entities averaged 9.1744 ms/frame. The configured 250 ms mean limits are coarse regression guards. They do not establish a 60 fps game budget, smooth frame pacing, world-streaming throughput or VRAM usage.

GitHub refuses CI before runners execute steps: “The job was not started because recent account payments have failed or your spending limit needs to be increased.” Example: [run 42036213824](https://github.com/Avrenthrad/lykowareGE/actions/runs/42036213824). This supplies no compiler/test result. Earlier merged work has historical Windows CI evidence; the pending stack has no fresh Windows/MSVC proof here. No billing changes or paid operations were performed.

Synthetic lifecycle tests pass, but actual Windows focus/minimize/restart and audio output require desktop execution. Pending #46 fixes known focus-loss input and minimized rendering behavior; main's green tests do not prove those fixed. The owner's GPU/VRAM is still unconfirmed. Software Vulkan cannot validate that hardware, driver behavior or flagship performance.

## Next actions and evidence

1. Fix the two ownership defects on their owning branches and add regression coverage before merging the affected modules.
2. Restore hosted CI through the owner and run Windows Debug/Release on the combined source. Keep all required merge checks.
3. Run actual Windows lifecycle/audio/GPU checks and collect Release frame-time, hitch, memory and VRAM measurements before judging Phase 1 exit readiness.

Use [LOCAL_VALIDATION.md](LOCAL_VALIDATION.md) for repeatable local evidence. The integration branch contains the runner, so Windows can execute `python tools/validation/verify.py` from a Developer PowerShell with prerequisites installed. Subsequent documentation additions do not change the frozen runtime snapshot above.

Raw reports remain local, with generated logs and captures excluded from Git:

- `/workspace/lykoware-health-main/report.json`
- `/workspace/lykoware-health-stack/report.json`
- `/workspace/lykoware-health-stack/scene.png`

The repository report preserves conclusions and repro source; raw paths are workspace artifacts and are not promised to persist across environments. This is a build/correctness evaluation, not an exhaustive security or legal audit.
