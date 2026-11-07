# ADR 0043 — A renderer capture checked against a committed golden

Status: Proposed.
Related task/PR: real-capture golden (branch `claude/capture-golden`), stacked on scripting PR 2 ([ADR 0042](0042-script-driven-scenes.md)).
Supersedes / superseded by: none. Uses the image harness ([ADR 0007](0007-image-harness.md)) and the CLI capture path ([ADR 0031](0031-scenes-and-command-replay.md)).

## Context

Phase 1's exit asks that "a real image capture passes its golden tolerance". Until now:

- **The image harness** compared software patterns with committed goldens.
- **`scene.cli_capture`** checked that two real captures of a scene are identical.
- **Missing:** no test compared a real capture with an expected image. A rendering regression that changed every capture the same way would have passed.

## Options

- **Reference:**
  - **a committed golden image**, small and binary;
  - **a hash** of the expected pixels;
  - **no reference**, repeatability only (what existed).

  A golden image was chosen. It can be compared with a tolerance, and a failure writes a diff image that shows what changed.

  Committing it fits the owner's rule against regenerable generated files. A golden is the expected result, written by a person and reviewed. A build never regenerates it.
- **Tolerance:**
  - **exact;**
  - **a small per-channel tolerance plus a small pixel allowance.**

  The tolerance was chosen. Driver builds (CI's lavapipe on LLVM 19 against LLVM 20 here) may round shading or flip edge pixels.

  The allowance must stay below the size of the smallest object that matters. The first try allowed 1% (192 pixels), and a deliberate break showed a whole crate (137 pixels) fitting inside it. So the allowance is 16 pixels.
- **Devices:**
  - **one golden per device class;**
  - **only the software rasteriser's golden for now.**

  Software only was chosen. It is what CI runs. Hardware GPUs are not required to match it to the pixel, so on them the test reports Skipped rather than inventing a pass.

## Decision

Proposed:

- **`tests/golden/falling-crates-160x120.ppm`:** the `lyko screenshot` capture of `samples/scenes/falling-crates` after its commands and 120 ticks, at 160×120.
  - It was taken on lavapipe (llvmpipe, LLVM 20.1.2) and looked at before committing.
  - It is 57,615 bytes.
- **`scene.capture_golden`** (`tests/capture_golden.py`) runs `lyko screenshot` and compares through the new helper `lykoware_image_compare`, which uses the harness's `compare_images` and `make_diff_image`.
  - **Tolerance:** at most 16 pixels may differ by more than 2 in any channel.
  - **Results:**
    - a failure writes the diff under the build directory;
    - it exits 77 (Skipped) without Vulkan or on a non-software device.
  - **Updating:** `LYKOWARE_UPDATE_GOLDEN=1` replaces the golden after a deliberate rendering change. The new image must be looked at and the change recorded.
- **`tests/golden/README.md`** documents both golden kinds and the update rule.

## Contracts and compatibility

- **New:**
  - the golden file;
  - the `scene.capture_golden` test;
  - the `lykoware_image_compare` test helper.
- **No engine or CLI change.**
- **A deliberate change** to the lit shader, the camera, the clear colour, the sample scene or its commands now needs a reviewed golden update in the same PR.
- **Not yet:**
  - goldens for hardware GPUs;
  - a moving scene: the capture is taken at rest, so interpolation is not covered here;
  - larger or textured scenes.

## Consequences

- The Phase 1 exit item "a real image capture passes its golden tolerance" has a test. Whether it counts as done is the owner's call.
- **CI caveat:** CI's lavapipe is an older LLVM. If its capture differs beyond the tolerance, the first CI run will show it, with a count and a diff image. The golden or the tolerance is then adjusted in a reviewed change.

## Verification

- **`scene.capture_golden`:** 0 of 19,200 pixels differ, in three runs.
- **Deliberate breaks:** 4 of 4 caught.
  - **Caught:**
    - the sky colour changed by 0.02: 8,360 pixels;
    - the commands after tick 60 skipped, so the blue crate stays: 137 pixels;
    - the ambient light changed from 0.25 to 0.27: 281 pixels;
    - the field of view 1% wider: 28 pixels.
  - **The first allowance (192 pixels) missed** the blue crate. The interpolation factor is not observable at rest (above).
- **Local results:** 56/56 pass in the Debug, ASan/UBSan and clang-tidy presets. The ASan build's capture also matches the golden with 0 differing pixels.
- **CI:** blocked for the whole account since 2026-11-05 (failed payment or spending limit, see STATUS).
