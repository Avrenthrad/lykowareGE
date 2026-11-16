# ADR 0103: save migration and last-good replacement

Status: accepted for 07.10. Extends ADR 0102.

Save format 3 adds a little-endian CRC32 (IEEE) of all preceding bytes as a
four-byte footer. The entry layout and world schema remain unchanged. The reader
also accepts format 2 from ADR 0102 and migrates it to the same owned State;
encoding always emits format 3. Version 2 cannot detect arbitrary payload bit
flips, so its guarantees are structural validation and explicit limits only.
Unknown formats/world schemas remain refused. CRC32 detects accidental corruption,
not malicious modification. No later-wave migration policy is claimed.

`read_file` reads a bounded regular file and decodes without mutating anything.
`replace_file` first validates the complete supplied bytes, migrates into an
owned journal and re-encodes the current format. Only then does it write a sibling
`<target>.pending`, flush and close it, and rename it over the target. Invalid or
truncated input never opens either file. A write/rename failure removes this
operation's pending file and leaves the previous target intact. A pre-existing
pending file is refused and preserved; callers may inspect or recover it manually.

The caller owns the target and reserved pending pathname in a trusted directory,
with a single writer across threads/processes. Renames require same-filesystem
replacement support (including Windows and Linux); failure is reported rather
than deleting the old target. There is no remove-then-rename fallback. Flushing
the C++ stream is not an fsync/directory-sync guarantee: power-loss durability is
not promised. No backup rotation, silent fallback to corrupted data or filesystem
mutation by decoding is introduced. No dependency or service is added.
