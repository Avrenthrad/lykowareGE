# ADR 0055: Authored sample character profiles

Status: Proposed. Continue #59 with a versioned JSON configuration for model path, idle/walk clip names and optional visual height. --character-profile FILE requires --play and cannot mix with individual character/clip/height flags. Relative model paths resolve against the profile directory. Refuse unsupported versions, unknown keys, invalid types and files over 64 KiB. This is sample locomotion setup, not a humanoid bone mapping or retargeting format.
