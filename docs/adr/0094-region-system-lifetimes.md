# ADR 0094: Navigation, audio and entities follow chunk residency (06.05)

Status: accepted. Owner: GPT. Extends ADR 0093.

ChunkLifecycle adds paired activate/deactivate callbacks to the chunk transaction.
Activation is after collision creation and all record allocations, before no-throw
publication. Failure rolls back incoming collision; activation must itself be
atomic. Every eviction and streamer destruction deactivates services before
releasing collision. Services and ECS must outlive the streamer. No lifecycle call
may occur during ECS iteration, hooks or concurrent physics/audio use.

RegionSystems supplies the production lifecycle adapter. The caller stages owned
RegionContent (nav solids, one shared immutable Sound bank handle, authored entity
positions) before streaming a previously unloaded region. On activation it copies
its own navigation grid, validates all solids, and creates StreamedEntity components
in the borrowed ECS world. Failed validation/creation rolls back new entities.
Successful activation consumes staged content. Inactive staged content can be
replaced; active content must first be evicted. The caller stages again on reload.

The grid is owned exclusively by RegionSystems, so private solid IDs cannot clash
with another nav user. Query access is const; removing a region erases all its
solids, entities and bank reference. Bank lookup returns a shared handle; external
copies may deliberately keep a Sound alive. This foundation does not start voices
or own decoded source files. Authored/staged data and service metadata are outside
the residency payload counter (hardware budgeting stays under 01.11).

All operations use one controlling simulation thread; no background ECS mutation.
The same sequence of stage/stream calls yields the same resource ordering and
navigation results. Replay evidence excludes process-local owner tokens. Missing
staged content, invalid solids/positions and locked ECS fail with diagnostics;
allocation errors propagate with incoming resources rolled back. Deactivate must
not throw and must be called outside ECS iteration; destruct in service lifetime
order. Real collision is still today's create/destroy API, not a physics rewrite.
