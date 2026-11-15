# ADR 0095: unloaded region asset edits

Status: accepted for 06.06.

An application-owned `RegionEdits` journal retains replacement asset bytes by
signed region coordinate and stable asset ID independently of `ChunkStreamer`
residency. The caller records edits and applies the journal to a fresh authored
`RegionData` before streaming it. Eviction never deletes journal entries. An
unedited region remains byte-for-byte authored. Clearing the journal deliberately
discards edits; destruction ends their lifetime. Disk persistence and versioning
are subsequent save work (06.07), not claimed here.

The journal has explicit positive byte and entry limits, separate from resident
cache limits. Empty replacements count as entries. Replacement admission credits
the prior entry's bytes. Refused edits leave prior entries intact. Application
validates that each edited ID occurs exactly once in the authored bundle and
publishes replacements together; a stale or ambiguous ID refuses the entire
application without modifying authored data. Asset IDs, dependency edges,
collision and subsystem definitions are unchanged by byte edits. Callers must
recook or explicitly update those definitions when their meaning changes.

One controlling thread owns all calls. Inputs are copied/moved into journal
ownership; applying copies bytes into the caller-owned bundle. Allocation failures
propagate with both journal and output unchanged. This API does not mutate a
resident chunk in place: apply before its next admission after eviction. It adds
no dependencies, filesystem writes or target-PC budget assumptions.
