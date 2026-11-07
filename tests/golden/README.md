# Golden images

Binary P6 PPM files. Git stores `*.ppm` as binary so pixel bytes are not rewritten as text. Tests write their captures and diff images under the build directory, never here.

## Image harness fixtures

These two belong to the GPU-free image harness (ADR 0007). They are software patterns, not renderer screenshots.

- `test-pattern-16x16.ppm` is the canonical 16 by 16 `TestPatternCapture`, compared at tolerance 0.
- `test-pattern-16x16-mismatch.ppm` matches that image except pixel (2, 3), which is RGB (1, 2, 3). The golden failure test expects that single mismatch.

## Renderer golden (ADR 0043)

- **The file:** `falling-crates-160x120.ppm` is the real renderer's capture of `samples/scenes/falling-crates.scene.json`, after its commands and 120 ticks, at 160×120.
  - It was taken through `lyko screenshot` on software Vulkan (Mesa lavapipe, llvmpipe LLVM 20.1.2).
  - It shows the grey ground, the red crate pushed right, the green crate, and the gold crate spawned at tick 30. The blue crate was destroyed at tick 90.
- **The test:** `scene.capture_golden` takes the capture again and compares it with `lykoware::compare_images`.
  - **Tolerance:** 2 per channel, and at most 16 pixels outside that. A single crate covers about 140 pixels, so a missing or extra object fails.
  - **Hardware GPUs** rasterise and round differently, so on them the test reports Skipped; their goldens are future work.
- **Updating it after a deliberate rendering change:**
  1. Run the test with `LYKOWARE_UPDATE_GOLDEN=1`.
  2. Look at the new image.
  3. Commit it with a note of what changed.

  Never update the golden to make a failure go away without knowing why it failed.
