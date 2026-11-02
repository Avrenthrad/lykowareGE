# ADR 0007 — GPU-free image harness

Status: Accepted for this implementation within the owner's instruction to add a GPU-free image capture and comparison harness.
Related task/PR: branch feat/phase0-image-harness, based on PR #6.
Supersedes / superseded by: none. This does not implement the graphics API. ADR 0002 accepts Vulkan 1.3 behind an owned RHI; this harness does not vendor or call it. This does not select an authored data format. ADR 0004 accepts canonical JSON, TOML config, and glTF 2.0 meshes; the PPM fixture is a test image, not that authored format.

## Context

Golden-image checks need a place to put pixels before a renderer exists. Calling that a screenshot would claim a capture the roadmap explicitly defers. An image library would be a new dependency for one test fixture.

## Options

- Keep image comparison out of the tree until Vulkan offscreen capture exists.
- Add a CPU-side image, a hand-written PPM reader and writer, and a tolerant comparison inside the existing engine library, fed by a deterministic software pattern.
- Add a third-party image codec or treat PPM as an authored project format.

## Decision

Keep the harness in `lykoware_engine`. `Image` is an owned 8-bit RGB buffer. `ImageCapture` is the fill interface. `TestPatternCapture` is the only implementation and is a software pattern, not a rendered frame. `write_ppm` and `read_ppm` are hand-written P6. `compare_images` uses a per-channel absolute tolerance and `make_diff_image` writes the absolute deltas that exceed it.

`lyko screenshot` is unchanged. It returns exit code 3 and prints `unsupported: no renderer`. The harness runs from CTest through `lykoware_image_harness_test`. That executable is the test-only path. It is not a screenshot command.

Real Vulkan offscreen capture plugs into `ImageCapture` later, on the path accepted in ADR 0002. No graphics backend is linked here.

## Contracts and compatibility

Ownership, lifetime, threading, and failure are specified on the declarations in [engine/include/lykoware/image.hpp](../../engine/include/lykoware/image.hpp).

Pixels are row-major RGB with the origin at the top-left, X to the right, and Y downward. There is no alpha and no row padding. Comparison is raw byte distance, not perceptual color. `image_harness_max_extent` (4096) is an allocation guard, not a display mode.

Canonical PPM is `P6`, decimal width and height, maxval `255`, then raw bytes. Newlines are `\n`. The file is not a scene, texture source, or save, and it has no schema version.

Committed fixtures live in `tests/golden/`. CTest writes round-trips and diff images only under the build directory.

## Consequences

Callers can round-trip a deterministic pattern and fail a comparison that exceeds its tolerance. They cannot capture a window, a swapchain, or a GPU frame. A pass of `image.harness.golden_pass` is not a screenshot gate and does not tick the image/performance checklist item. Raising the extent guard, or accepting another pixel layout, is a contract change.

## Verification

`image.harness.api` covers the pattern, PPM round trip, malformed input, write failure, tolerance, and diff image. `image.harness.golden_pass` matches the committed 16 by 16 pattern at tolerance 0. `image.harness.golden_fail` requires the committed one-pixel mismatch to fail tolerance 0 and to produce a diff. `cli.commands` and `cli.integration` keep `screenshot`, including extra arguments, at exit 3. Record platform results in STATUS rather than treating this plan as a pass.
