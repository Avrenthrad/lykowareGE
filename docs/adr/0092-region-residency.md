# ADR 0092: Explicit region residency and eviction (06.03)

Status: accepted. Owner: GPT.

A Residency owns each region's immutable asset bundle: stable string IDs, owned
bytes and dependency IDs. Dependencies must resolve within the bundle and form an
acyclic graph. Duplicate IDs, missing dependencies and cycles are rejected before
publication. Bundles are independent; shared IDs in different regions are copied
and charged per region. The asset cooker can supply their payloads later.

The caller supplies nonzero byte and region-count limits. Bytes measure payload
sizes, not allocator metadata, IDs, dependency lists or temporary loads. There is
no silent default or claimed target-PC memory budget. Empty payloads still consume
a region slot. Lookup touches LRU order; contains/bytes/size do not. Installation
makes a region most recent. Eviction removes the least recent unpinned regions
until both limits hold. Pins protect all assets held by a region; pinned regions
cannot be replaced or erased. If pins prevent admission, the whole install fails
and neither contents nor LRU order change. Unpinning does not itself evict.

Install stages a new list with immutable shared bundles; validation and allocations
finish before swapping into place. The old resident state survives failure.
Borrowed region data remains valid only until the next successful mutation or
cache destruction. This cache is single-threaded; jobs return owned load results
for the controlling thread to install. Allocation errors propagate. The cache
never writes ECS, physics or renderer state. Chunk owners must mirror its eviction
results; the subsequent chunk layer supplies that integration.
