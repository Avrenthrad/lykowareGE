# ADR 0001 — Runtime language

Status: Proposed. Joshua requested comparison before choosing. No migration authorized by this ADR.

## Context and evidence

The repository already has a small C++20/CMake library and sample; Windows Debug/Release and Linux checks passed ([baseline CI](https://github.com/Avrenthrad/lykowareGE/actions/runs/40265394221)). Rust has not been prototyped here. There is little code to migrate today; future native-library boundaries matter more than sunk cost.

| Criterion | C++20 | Rust |
| --- | --- | --- |
| Memory safety | RAII helps; unsafe memory/data races need discipline, sanitizers and review | Safe Rust prevents many classes; unsafe/FFI and logic defects still require review |
| Jolt integration | Direct native API; simplest integration path | C++ bindings, cross-language lifetime/thread contracts and native build tooling |
| Build/tooling | CMake/MSVC already proven; dependency management needs conventions | Cargo workspace, rustfmt/clippy/tests standard; native integrations still need C/C++ builds |
| Graphics options | Vulkan/other APIs through replaceable wrappers | wgpu portability or ash Vulkan control; evaluate feature coverage and licensing |
| AI-written code | Strong compiler warnings plus tests/sanitizers needed | Compiler/borrow checker offers additional feedback; agents can still misuse unsafe or design APIs poorly |
| Performance | Suitable for low-level engine work | Also suitable; neither language proves frame-time or throughput superiority here |
| Complexity | One language across core and Jolt bridge | Rust plus a deliberately small C++ FFI layer |
| Current evidence | Working small baseline, no full engine | No repository measurements yet |

## Comparison task

Build equally scoped, disposable C++ and Rust spikes on separate task branches: window/events/timing, serialized command invocation, and a minimal Jolt body lifecycle behind a narrow interface. Do not pull in Bevy or another complete engine. Vendor approved low-level sources rather than depending on moving remote branches.

Measure clean/incremental build time, artifact/runtime memory, deterministic fixture throughput, error diagnostics, binding complexity, licence inventory and Windows portability. Record machines, versions and commands. Avoid judging a language from an unrepresentative empty loop.

## Decision and exit

Open. Recommend a choice after the comparison report and resolve it with Joshua. If Rust wins, plan migration and retain the C++ baseline history; add only crates that have actual callers. If C++ wins, specify warning/sanitizer/static-analysis coverage. Do not create an empty multi-crate architecture or delete working targets before the decision.

## Consequences

Phase 0 language-dependent CLI/workspace implementation follows the decision. Graphics/physics/data ADRs can be evaluated in parallel conceptually, but their implementation must match the chosen stack.
