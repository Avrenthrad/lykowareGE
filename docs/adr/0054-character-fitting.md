# ADR 0054: Explicit visual character fitting

Status: Proposed. Continuation of #58.

Sandbox --character-height METRES explicitly fits imported bind-mesh bounds to a visual height, centres X/Z and aligns its lowest bind point to the controller's feet. Apply the same uniform scale and origin offset outside the skin palette, preserving authored skeletons, inverse binds and clips. Capsule/camera/physics remain unchanged. Without this option authored placement is retained. Refuse invalid heights and zero-height bounds; report bind bounds and joint/clip counts. Animated bounds may exceed bind bounds: this is not automatic collision fitting or retargeting.

Verification: source 1aab213 passes 66/66 each Linux Debug, ASan/UBSan and clang-tidy 18, no skips, clean unchanged inputs and successful staging/installed CLI. Imported fitting CLI passes with synchronization validation. Draft #59; Windows and hardware remain unverified.
