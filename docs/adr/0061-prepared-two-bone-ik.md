# ADR 0061 — Prepared two-bone IK and reusable scratch

Status: Proposed. Owner: Codex. Date: 8 November 2026. Base: main `6262089`.

## Contract

Keep `pose_two_bone` as a correctness reference. Add immutable `PreparedTwoBone`, created from a validated skeleton and direct root/knee/end chain. Preparation owns a compact parent snapshot and cached rest segment lengths; it retains no reference to caller-owned skeleton data. Later skeleton edits require explicit re-preparation. Unit rest scales, supported nonzero lengths and parent-before-child ordering remain mandatory; no new helper-chain, scale, foot-orientation or walking-lock support.

A caller-owned `TwoBoneScratch` has fixed storage bounded by `animation::max_joints`. Prepared apply accepts complete input/output spans of the prepared joint count, permits overlap/aliasing, validates dynamic pose values/unit scales/unchanged limb lengths, and commits output and solution only after FK verification. Scratch is unspecified after success/failure and must not be shared concurrently. Immutable plans may be shared with independent scratch/output. Successful prepared application performs no heap allocations. Failure messages may allocate. Preparation may allocate and throw; invalid preparation returns empty with an explanation.

The sandbox foot controller prepares both independent legs once, retains candidate/FK/scratch storage and writes output only after both legs succeed. Its physics, smoothing, reset and bilateral transaction contracts remain unchanged. The playable body retains corrected-pose output storage across frames. Caller output capacity may still allocate on first use; no whole-engine allocation-free or crowd-performance claim is implied.

## Verification

Compare prepared results to the unchanged reference for varied 3D targets, rotated parents, reach clamps, fallbacks and aliasing. Independently check FK and preservation of unaffected joints. Reject invalid preparation, sizes, numeric values, scales and changed lengths with unchanged public outputs. Verify owned snapshot lifetime and explicit re-preparation. Count successful prepared-apply allocations after warmup. Retain terrain/controller reset/contact/physics-state regression coverage and imported-render-palette checks. Run Windows Debug/Release and Linux Debug/sanitizer/analysis before merging; actual target-PC performance remains a separate gate.
