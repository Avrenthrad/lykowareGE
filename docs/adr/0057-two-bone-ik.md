# ADR 0057: Two-bone position IK reference

Status: Proposed. Continue #61 with an engine-owned geometry solver before pose/terrain integration.

Input is root, target and a bend-hint point in a shared double-precision coordinate space, two positive lengths and an explicit fallback forward direction. Return root, knee and end positions plus reach-clamping status. No stretch. Clamp unreachable targets to the geometric reach interval. A target at root uses the fallback direction; equal-length limbs fold fully, unequal limbs clamp to their inner radius. A collinear hint uses a deterministic perpendicular axis, reported in the result. Refuse nonfinite inputs, lengths outside 1e-6..1e6 metres and unrepresentable/collapsed output geometry. Output is unchanged on failure. This is drawing geometry, not authoritative deterministic simulation, joint rotation application, foot IK or physics.
