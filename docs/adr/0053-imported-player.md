# ADR 0053: Imported playable character clips

Status: Proposed. Codex continuation of #57.

Sandbox --play accepts --character FILE plus optional --idle-clip NAME and --walk-clip NAME. Import uses the owned glTF skinned reader and existing material upload; authored metres, origin at feet and forward -Z are retained. Named clips must exist uniquely; absent selectors use first clip for idle and second (or first when only one exists) for walk. Refuse models without clips. This first adapter uses the sample locomotion playback, not an engine humanoid retargeter. Imported models do not receive the procedural visor. Physics capsule remains unchanged; rig fitting, root motion, IK and retargeting remain later work.

Verification: source aabb116 passes 66/66 each Debug, ASan/UBSan and clang-tidy 18 without skips, clean unchanged source; installed CLI passes. Imported playback also passes Debug with synchronization validation enabled. Windows remains unverified. Draft #58; no merge.
