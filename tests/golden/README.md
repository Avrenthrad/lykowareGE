# Image harness fixtures

These PPM files belong to the GPU-free image harness. They are not renderer screenshots. Real screenshot tests wait for a renderer. `lyko screenshot` still returns `unsupported: no renderer`.

- `test-pattern-16x16.ppm` is the canonical 16 by 16 `TestPatternCapture`, compared at tolerance 0.
- `test-pattern-16x16-mismatch.ppm` matches that image except pixel (2, 3), which is RGB (1, 2, 3). The golden failure test expects that single mismatch.

Both files are binary P6 written by the harness. Git stores `*.ppm` as binary so pixel bytes are not rewritten as text. CTest writes its copies and diff images under the build directory, not here.

A later Vulkan offscreen capture (ADR 0002) can feed this comparison. It is not implemented.
