# Streaming traversal fixture — 06.11

Grok can wrap the built `lykoware_streaming_throughput_test` executable without
changing its arguments. It is headless and uses real static Jolt collision. The
`lykoware_streaming_throughput_fake_test` target is only a fixture verification
backend; CSV rows explicitly say `fake` or `jolt`.

Arguments, all required:

`CSV STEPS CHUNK_BYTES RESIDENT_BYTES RESIDENT_REGIONS STEP_BUDGET_MS`

Hosted smoke-test values: `traversal.csv 64 4096 8192 2 16.666667`.
The timing value is an explicit test input, not an agreed target-PC budget.
Run the real target from the configured build directory on Windows or Linux.
The CSV and its sibling `CSV.region.bin` are overwritten; use a dedicated output
directory. Generation and CSV formatting are outside the measured step.

Columns:

| Column | Meaning |
| --- | --- |
| step, region_x, region_z | Deterministic traversal index and signed destination |
| load_ms | Worker dispatch/read/fence, owned snapshot copy and byte verification |
| activation_ms | Payload construction, chunk admission, collision create/eviction |
| step_ms | Combined serialized load/admission interval |
| hitch_ms | max(0, step_ms - explicit step budget) |
| resident_bytes | Resident asset payload only; excludes metadata/transient/source bytes |
| resident_regions, collision_bodies | Post-admission resource counts |
| backend | `jolt` for real physics, `fake` for fixture checks |

One worker executes FIFO requests; a completion fence avoids polling. OS page
cache is normally warm after the first read. This is serialized CPU traversal,
not a cold-disk benchmark or a rendered-frame/GPU/audio/nav/entities workload.
Record the command arguments, commit, build preset, OS/driver/hardware and external
harness frame/RSS measurements separately. No timing threshold fails hosted CI.

**Joshua runs this** on the RTX 4070 Windows PC using agreed budgets and Grok's
harness. Linux/hosted smoke timings do not tick 06.11; PC acceptance stays open.
