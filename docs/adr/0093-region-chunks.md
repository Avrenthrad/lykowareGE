# ADR 0093: Resident chunk payloads and collision (06.04)

Status: accepted. Owner: GPT. Uses ADRs 0089 and 0092.

A caller supplies a decoded ChunkDefinition for the region of a moving double
position. Each definition owns its region bundle, coarse/fine asset IDs and pose,
plus absolute-world static physics body descriptions. No shader or renderer work
is included. A resident region is reused and touched rather than re-authored;
evict before applying authored changes. Decode and asynchronous reads remain
separate from the controlling-thread installation step.

ChunkStreamer stages residency admission, then creates new static collision
through today's physics create_body/destroy_body API. No physics source changes.
If any creation fails, all new bodies are rolled back and previous residency stays
intact. Publication swaps staged cache/records, then destroys every evicted body's
handle. The physics world needs headroom for incoming and outgoing bodies during
this transaction. A failed admission does not unload the previous region.

ChunkCollision exposes create/destroy callbacks so the transaction can be tested
without a physics backend. physics_chunk_collision supplies the production World
adapter. Callbacks must be paired, serialised, and destroy must not throw. Captured
services must outlive the streamer. The streamer owns created handles and destroys
them on eviction/destruction. Callers must not destroy these bodies independently.

Views expose stable region ID, double pose, and spans of coarse/fine bytes. The
streamer owns those bytes; the renderer borrows them for one frame. Re-query after
any streaming/pin mutation; do not retain views across mutation/destruction. Pins
use the residency rule; no hardware budgets are invented. Invalid definitions,
positions, missing payloads and backend failures return false with diagnostics;
allocation errors propagate after resource rollback. All calls are on the owning
simulation thread, outside ECS/physics callbacks and iteration.
