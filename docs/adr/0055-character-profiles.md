# ADR 0055: Authored sample character profiles

Status: Proposed. Continue #59 with a versioned JSON configuration for model path, idle/walk clip names and optional visual height. --character-profile FILE requires --play and cannot mix with individual character/clip/height flags. Relative model paths resolve against the profile directory. Refuse unsupported versions, unknown keys, invalid types and files over 64 KiB. This is sample locomotion setup, not a humanoid bone mapping or retargeting format.

Verification: source c469c81 passes 66/66 each Linux Debug, ASan/UBSan and clang-tidy 18 without skips, clean unchanged source; staging/installed CLI pass. Profile CLI also passes synchronization validation. Draft #60; Windows/hardware unverified.
