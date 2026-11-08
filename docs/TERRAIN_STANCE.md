# Stationary terrain stance constraints

ADR0066 extends the existing opt-in direct-chain foot IK with surface orientation and visual pelvis compensation. This is stationary presentation support, not walking foot locking, a balance controller or physics-driven animation.

A version-1 character profile can add:

```json
{
  "foot_ik": true,
  "foot_align_normals": true,
  "foot_max_slope_degrees": 35,
  "foot_pelvis": true,
  "foot_max_pelvis_drop": 0.15
}
```

These fields augment the required model and seven mapped bone names; they are not a complete profile by themselves. Existing profiles retain vertical-only behavior. Either stance flag requires `foot_ik`; a slope bound requires either stance flag, and a drop bound requires pelvis mode. Slope is finite 0..60 degrees (default35); drop is finite 0..0.3 world metres (default0.15). The profile parser, inspection CLI and actual sandbox playback share these settings. `lyko inspect-character --profile FILE` reports effective `foot_policy`.

`physics::RayHit::normal` is the read-only world-space geometric surface normal for the actual hit subshape. Existing body/fraction/point fields retain their meaning. Nonfinite directions are refused; no vendor type escapes. Terrain normals reflect the actual compressed collision geometry, which can differ from the original sampled heights. Rays beginning inside a solid retain the underlying shape query's representative-normal semantics; this is not a penetration-depth query.

`animation::PreparedJointAxis` owns parent topology and calibrates a joint-local axis from a rest-model direction. Apply rotates that posed axis toward a model-space target through a bounded shortest arc, changing only the chosen local rotation. It preserves that joint's origin; descendants follow the rotation. Unit-scale complete poses only. Complete spans may alias/overlap. Output and solution commit after FK verification. Immutable plans can be shared with separate reusable TwoBoneScratch; skeleton edits require re-preparation. Setup/failure strings may allocate, successful application reuses storage.

The sample calibrates each foot's rest-model +Y; it does not assume exporter joint-local Y points upward. Accepted normals are smoothed in world space and transformed through actor rotation, excluding uniform visual scale. Downward or too-steep surfaces are rejected in either stance mode. Ankle orientation is applied after leg position IK, with a maximum correction equal to the configured slope angle. If animation/IK already rotates an ankle far from the target, the correction clamps and increments diagnostics; full alignment is not guaranteed beyond that budget. The existing artist-calibrated clearance still measures the foot joint's vertical world height, not automatic mesh-sole clearance.

Pelvis mode uses both accepted contacts to choose a downward-only body correction. The cap is the smaller of the authored drop and a quarter of the shorter calibrated full leg's **world** length. Exponential smoothing uses the existing 12/s rate. Convert world-down to model space with actor rotation/scale and to pelvis-local space through its posed parent. Then solve both legs toward their original world foot targets. No collision body, camera, limb length or authoritative world state is changed. A missing foot contact clears pelvis compensation; unreachable feet retain the existing clamp diagnostics. This does not add stretch, knee limits, hip balance or a general body constraint solver.

The controller remains active only when grounded, stationary and visible. Losing contacts, becoming inactive, teleporting by more than1m or invalid/>0.25s updates resets correction state. Runtime refusal preserves caller pose, both legs' persistent state, pelvis offset and statistics; callers retain base animation. Diagnostics include `oriented`, `slope_rejected`, `pelvis_frames` and existing contact/clamp/reset/failure/applied-palette counts. Sockets are evaluated from the final corrected pose in the sample.

Verification: `animation.joint_axis`, `animation.terrain_stance`, existing IK/foot-placement/physics/camera tests, actual `sandbox.imported_player` and `render.gpu_skinning`. The latter saves `gpu-skinning/terrain-stance.ppm`, comparing GPU pixels with independently CPU-deformed geometry at the existing tolerance. The image shows original rest stance at left and corrected slope stance at right. Test assets are original debug geometry. Tests count ordinary C++ allocations after setup; no crowd/hardware performance guarantee is claimed. Physical-PC feel and calibrated budgets remain deferred.
