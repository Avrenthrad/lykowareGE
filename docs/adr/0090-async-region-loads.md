# ADR 0090: Owned asynchronous region reads (06.02)

Status: accepted. Owner: GPT. Builds on ADRs 0077 and 0089.

`RegionLoad::start` schedules a bounded file read on the existing jobs queue and
returns an owning request handle. Callers supply a region, file path and nonzero
byte limit; there is no implicit memory budget. The synchronous `load_region_bytes`
uses the same reader. Payloads are raw authored region bytes; chunk decoding and
main-thread installation come later. File paths are copied before scheduling.

Each request owns a job cancellation fence and shared result state, never a raw
pointer captured by a worker. States are pending, loading, ready, failed and
cancelled. Status/result snapshots own their bytes. Only a ready result contains
payload. Cancel immediately clears the request's resident result; the worker
checks cancellation between bounded read blocks and again while publishing under
the result mutex. Cancel wins over a completed but unconsumed result too. Copies
already returned to the caller remain owned by that caller.

Queue refusal becomes a failed request with a diagnostic. Missing files, zero
limits, nonregular files, oversize inputs, short reads and files that grow during
a read fail without a partial result. I/O errors are reported, allocation errors
propagate synchronously or become failed async results. File contents must remain
immutable during a read; same-size concurrent edits are not detected. No file
watcher, remote fetch, decompression or external dependency is introduced.

All public methods require one controlling thread. Workers only read files and
publish owned results; they never touch ECS or physics. The queue is borrowed
only during start. Destruction cancels, then the owner fence waits for any active
callback; queued callbacks are invalidated. Filesystem I/O itself is not forcibly
interrupted, so shutdown can wait on an OS read. The queue must follow its existing
single-controller lifecycle contract. A cancelled handle never installs assets.
