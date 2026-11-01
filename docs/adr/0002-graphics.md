# ADR 0002 — Graphics API and RHI

Status: Proposed.

## Direction and options

Vulkan is the proposed primary API, with a Lykoware-owned rendering hardware interface (RHI) and room for D3D12 if Windows/Arc measurements justify it.

If Rust is selected, compare wgpu (portability, validation, feature limits and backend control) against ash (direct Vulkan control and more explicit synchronization/resource work). For C++, evaluate direct Vulkan through a narrow native implementation. Do not assume wgpu itself selects Vulkan on Windows; backend selection must be explicit and tested. No dependency is selected yet.

## Contract and evidence needed

Define device/resource lifetime, render thread ownership, uploads, synchronization, surface resize/device loss, shader build/caching and error capture. Version cook outputs and invalidate pipeline caches by device/driver compatibility.

Run identical capture/performance fixtures on the specified Intel Arc and RTX devices. Verify shaders compile/cook before traversal and record pipeline/cache misses and stalls. Cache persistence cannot guarantee zero hitches across new hardware/drivers.

## Exit

Choose an implementation with recorded feature/licence coverage and measured frame-time/memory results, using the chosen language. Headless GPU capture and UI rendering must share rendering paths. Do not choose the backend solely from an API preference.
