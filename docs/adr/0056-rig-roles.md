# ADR 0056: Explicit humanoid leg roles

Status: Proposed. Continue #60. Optional profile `bones` maps pelvis, left_hip, left_knee, left_foot, right_hip, right_knee, right_foot to exact unique skeleton joint names. If provided, all seven roles are required and distinct. Each leg must follow pelvis → hip → knee → foot ancestry; intermediate helper joints are allowed. Resolve against the loaded skeleton, not glTF skin-list indices. Reject missing/ambiguous names and invalid topology before uploading the character. Print resolved roles/indices for inspection. Mapping alone does not enable IK, retargeting or alter animation/physics.

Verification: source 1699fc8 passes 66/66 each Linux Debug, ASan/UBSan and clang-tidy 18, no skips, clean unchanged source and successful staging/installed CLI. Mapped imported playback also passes synchronization validation. Draft #61; Windows/hardware remain unverified.
