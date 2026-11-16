# ADR 0102: versioned region edit saves

Status: accepted for 06.07.

`save.hpp` encodes the application-owned region edit journal, independently of
residency. The format is explicit little endian: four bytes `LYKW`, u32 save
version (2), u32 world schema (1), u32 entry count; each entry contains i32 region
X/Z, u32 asset-ID byte length, ID bytes, u64 payload byte length, payload bytes.
Entries follow the journal's signed-coordinate/ID order. No native structs,
pointers, timestamps or host-size integers are serialized. Empty payloads are
valid edits. IDs are opaque nonempty byte strings; they are not filesystem paths.

Both encode and decode require explicit positive file-byte, payload-byte, entry
and ID-byte limits. Parsing checks lengths against remaining input and limits
before allocation. Duplicate edits and trailing bytes are refused. Unknown save
versions and world schemas are refused; no newer data is guessed at or silently
discarded. Current saves round-trip into an owned `State` that can be applied by
recording its entries into a new journal. Decode never mutates a live world.

This step provides a byte codec, not filesystem recovery. Older-version migration,
integrity checks and preserving the last good file are 07.10. No unlimited
default, new dependency, renderer/physics serialization or hardware budget is
introduced. All calls use one controlling thread. Result values own their bytes;
input spans are borrowed only for the call. Malformed input returns an empty
optional and an error; allocation failures propagate.
