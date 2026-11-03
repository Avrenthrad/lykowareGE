# Vulkan Memory Allocator provenance

This directory holds the single-header Vulkan Memory Allocator (VMA). It is not a Lykoware-owned library. Upstream copyright stays with Advanced Micro Devices, Inc.

| Field | Value |
| --- | --- |
| Name | Vulkan Memory Allocator |
| Upstream | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator |
| Tag | `v3.4.0` (`VMA_VERSION` 3.4.0, released 2026-06-05) |
| Commit | `3aa921224c154a0d2c43912bc88e1c42ce1f7607` |
| Obtained by | `git clone --depth 1 --branch v3.4.0` of the upstream repository |
| Vendored-file sha256 | `d9fb32955960c67182b66f5dc34d9e4c9c81e23d10f4b3ff0c7ce5ffa38bc582` (see below) |
| SPDX licence | MIT |
| Licence file | [LICENSE.txt](LICENSE.txt) |
| Wrapper | `lykoware::rhi` in the engine library. Only `engine/src/rhi/` includes `vk_mem_alloc.h`. |
| Local source patches | None. Present files are byte-identical to upstream. |

## Why 3.4.0 and not 3.5.0

3.5.0 was released on 2026-10-26, one week before vendoring. 3.4.0 has been public for five months. Lykoware does not use `vmaImportVulkanFunctionsFromVolk`, which 3.5.0 fixes; it passes volk's `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` to VMA with `VMA_DYNAMIC_VULKAN_FUNCTIONS`. Move to a newer release deliberately, with its changelog reviewed.

## What is included

`include/vk_mem_alloc.h` and `LICENSE.txt`. The implementation is compiled once, in `engine/src/rhi/vma_impl.cpp` (a Lykoware file), as the static target `lykoware_vma` without Lykoware's warning flags.

## What was omitted

`src/` (samples and tests), `docs/`, `media/`, `tools/`, `bin/`, `CMakeLists.txt`, `Doxyfile`, `CHANGELOG.md`, `AGENTS.md` and the upstream README.

`PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Integrity check

The vendored-file hash covers every upstream file in this directory, excluding `PROVENANCE.md`. From this directory:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```
