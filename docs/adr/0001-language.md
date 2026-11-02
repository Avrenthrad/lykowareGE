# ADR 0001 — Runtime language

Status: Accepted — Joshua selected the C++ route after the initial comparison discussion.

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

The graphics row records the options considered while choosing a language. ADR 0002 later rejected wgpu and ash and accepted Vulkan 1.3 from C++.

## Decision

Use **C++20 with CMake** for Lykoware's runtime and native tooling. Continue from the verified engine/sample targets. Prefer direct native library integrations behind Lykoware-owned interfaces; Jolt remains a proposed dependency, not selected by this language decision.

Joshua's later instruction, "Let's go C++ route I think", resolves the earlier comparison request. No C++/Rust implementation spikes or comparative performance measurements were completed; this is an owner decision informed by the qualitative tradeoffs above, not a benchmark result.

## Rationale and tradeoffs

C++ keeps one native language across the runtime and likely physics integration, preserves working Windows builds, and avoids introducing Rust/C++ binding ownership for the initial engine. Rust's memory-safety benefits remain valid; choosing C++ requires explicit ownership and verification discipline. This decision does not claim C++ is universally faster or safer.

## Planned quality coverage

- Keep current MSVC /W4 /permissive- and GCC/Clang -Wall -Wextra -Wpedantic warning coverage.
- Add AddressSanitizer coverage for applicable native tests on a supported CI configuration.
- Add UndefinedBehaviorSanitizer coverage on GCC/Clang where supported, and targeted ThreadSanitizer coverage when concurrent systems are introduced.
- Add a pinned clang-tidy configuration for changed C++ modules and document exceptions.
- Use RAII, explicit lifetimes, reviewed public APIs, and executable/unit/integration tests.

The warnings exist in the baseline. Sanitizer and clang-tidy coverage is recorded in the implementation follow-up; do not describe a check as passed without the evidence in STATUS.

## Consequences

No Rust migration, Cargo workspace, or language-comparison spike is required. Build the lyko CLI and future runtime modules as target-based CMake components with actual callers. Graphics and authored formats are accepted in ADRs 0002 and 0004. Physics remains ADR 0003. Preserve vendoring/licensing, Windows-first testing and engine/tool boundaries.

## Implementation follow-up

The native CLI change centralizes /W4 or -Wall/-Wextra/-Wpedantic, adds /WX/-Werror by default, Linux ASan/UBSan presets and clang-tidy 18 configuration/jobs. These implement the earlier planned coverage; see STATUS and the task PR for actual results. ThreadSanitizer remains deferred until concurrent engine systems exist. This follow-up does not change the accepted language decision.
