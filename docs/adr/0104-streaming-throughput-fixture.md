# ADR 0104: headless streaming measurements

Status: accepted for the fixture portion of 06.11. PC acceptance remains open.

The standalone `lykoware_streaming_throughput_test` traverses deterministic signed
regions, reading a generated binary fixture through `RegionLoad` on one FIFO job
worker before admitting each chunk on the controlling thread. It uses real static
physics create/destroy and checks resident queries/body counts. A separate fake
backend target verifies the same fixture without a physics build.

Arguments explicitly supply CSV path, step count, bytes per chunk, resident byte
limit, resident region limit and step budget in milliseconds. There are no silent
memory or timing budgets. The source fixture is generated beside the CSV, outside
the measured interval; reads will normally use the OS page cache after the first
step. Each step starts a load and waits for a FIFO completion fence; this measures
serial load/admission latency, not overlapping throughput under contention.

CSV columns: `step,region_x,region_z,load_ms,activation_ms,step_ms,hitch_ms,`
`resident_bytes,resident_regions,collision_bodies,backend`. `load_ms` includes job
dispatch, file read, completion fence, owned snapshot copy and byte verification. `activation_ms`
includes chunk construction/admission/collision creation and eviction. `step_ms`
ends before validation and CSV formatting. `hitch_ms` is max(0, step_ms minus the
explicit step budget). Resident bytes are the cache's payload counter, excluding
authored sources, snapshots, metadata and transient copies; they are not process
RSS. Rows name the backend so fake timings cannot be mistaken for Jolt timings.

All timings use a steady clock. No rendered frame, GPU, audio scheduling, nav or
entity workload is measured. This supplies CPU traversal load/overrun evidence for
Grok's harness; it is not RTX 4070 frame-pacing acceptance. Joshua runs this on the
Windows PC with agreed limits and records the external harness/environment data.
06.11 stays unticked until that evidence is accepted. No new dependency/service.
