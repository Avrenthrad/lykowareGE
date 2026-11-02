# ADR 0002 — Graphics API and RHI

Status: Accepted — Vulkan 1.3 from C++ behind a Lykoware-owned RHI. No graphics dependency is vendored by this decision.

## Context

The brief requires a cross-vendor graphics path, including Intel Arc and Linux, zero stutter from prepared shaders (section 8), and headless capture on the same render path as interactive drawing. C++20 is accepted in ADR 0001, so a Rust-only graphics library cannot be the RHI.

## Options

- Vulkan 1.3 called directly from C++, behind a thin render hardware interface (RHI) owned by Lykoware.
- D3D12 as a later backend, only if Intel Arc measurements justify it.
- wgpu and ash. Rejected because they are Rust-only (ADR 0001).

## Decision

Vulkan 1.3 is used directly from C++, behind a thin render hardware interface (RHI) owned by Lykoware.

Vulkan-Headers, the volk loader and Vulkan Memory Allocator are vendored and pinned under third_party, wrapped per ADR 0000, with exact licences and versions recorded at vendoring time. This ADR does not add those sources.

Shaders are compiled ahead of time to SPIR-V, plus device/driver-keyed pipeline caches, for zero stutter (brief section 8). The shader-compiler licence must be reviewed as a build-time tool before that compiler is adopted.

Headless capture renders offscreen through the same RHI path, to meet the shared-path rule. A software Vulkan driver such as Mesa lavapipe in CI is a follow-up to evaluate.

D3D12 is a possible later backend, only if Intel Arc measurements justify it.

wgpu and ash are rejected because they are Rust-only (ADR 0001).

## Contracts still required before implementation

Define device/resource lifetime, render-thread ownership, uploads, synchronization, surface resize and device loss, shader build/caching and error capture. Version cook outputs and invalidate pipeline caches by device/driver compatibility.

Run identical capture and performance fixtures on the specified Intel Arc and RTX devices once those profiles exist. Cache persistence cannot guarantee zero hitches on new hardware or drivers.

## Consequences

There is one render path for on-screen drawing and headless capture. No graphics library, shader compiler or software Vulkan driver is added by this document. D3D12 is not a committed backend. Feature coverage, frame time and memory are unmeasured. Accepting the approach does not mean zero stutter has been achieved.

## Verification

Acceptance is the recorded decision above. It is not a measured bake-off. Before a graphics dependency lands, record its exact version, licence and notices, and show a caller. Headless and interactive rendering must share the RHI. Lavapipe, or another software Vulkan driver, in CI remains a follow-up to evaluate.
