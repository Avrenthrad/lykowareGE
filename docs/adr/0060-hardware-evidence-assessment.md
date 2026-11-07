# ADR 0060 — Local hardware evidence and offscreen assessment

Status: Proposed. Owner: Codex. Base: main `992e791`. Date: 8 November 2026.

## Contract

Extend the local Windows measurement runner, not the runtime API. Hardware metadata version 2 records source commit/dirtiness, executable SHA-256, fixture hashes/configuration and optional NVIDIA inventory (name, total MiB, driver); no hostname, username, UUID or automatic upload. Query `nvidia-smi` when available; failures leave inventory unknown. Never infer VRAM from model names or WMI AdapterRAM. Inventory lists all adapters and does not identify the Vulkan-selected adapter by itself.

Each run uses a new output directory. A Python assessor reads hardware.json and exactly three scene reports, validates schemas/configuration, hardware rendering, sample counts, finite nonnegative durations, replay/final-state agreement and device identity across runs. It recomputes nearest-rank p99, mean/max and records process peak RAM. Invalid, missing or inconsistent inputs fail with a saved assessment. GPU inventory absence produces incomplete evidence, not a hardware acceptance pass.

Optional `--budget FILE` / PowerShell `-BudgetFile` accepts versioned provisional offscreen mean/p99/max ceilings in milliseconds and process-RAM bytes. No thresholds default silently; thresholds must be supplied as reviewed data. Equal-to-limit passes. Overruns fail. A satisfied budget applies only to this synchronous offscreen fixture; it does not accept the recommended profile, presentation pacing, memory residency, streaming, AI or Phase 1. Phase 1 acceptance is always false in this report. Reports are local diagnostic evidence, not tamper-proof attestations or proof a custom executable matches the source commit.

## Verification

Exercise malformed/missing reports, software GPU, nonfinite/negative samples, count/configuration mismatch, replay/device mismatch, absent GPU inventory, and exact-boundary/overrun budget cases. Register with CTest; Windows CI also runs the PowerShell runner with synthetic NVIDIA inventory, verifies >4 GiB memory metadata, and requires unavailable Vulkan to remain pending and existing directories to be refused. Physical GPU inventory/measurements remain target-PC work.
