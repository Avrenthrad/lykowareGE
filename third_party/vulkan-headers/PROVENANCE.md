# Vulkan-Headers provenance

This directory holds the C headers of the Khronos Vulkan-Headers repository. It is not a Lykoware-owned library. Upstream copyright stays with The Khronos Group Inc.

| Field | Value |
| --- | --- |
| Name | Vulkan-Headers |
| Upstream | https://github.com/KhronosGroup/Vulkan-Headers |
| Tag | `vulkan-sdk-1.4.363.0` (`VK_HEADER_VERSION` 363) |
| Commit | `6802bb4733b63ed5efd3adb308a6c885ef180ea1` (committed 2026-09-18) |
| Obtained by | `git clone --depth 1 --branch vulkan-sdk-1.4.363.0` of the upstream repository |
| Vendored-file sha256 | `812bacd651575786f5274b7dfa568cbe646fabda63290c14870ab30f2cdda200` (see below) |
| SPDX licence | `Apache-2.0 OR MIT` on every vendored header |
| Licence files | [LICENSE.md](LICENSE.md), [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt), [LICENSES/MIT.txt](LICENSES/MIT.txt) |
| Wrapper | `lykoware::rhi` in the engine library. Public engine headers do not include Vulkan headers. |
| Local source patches | None. Present files are byte-identical to upstream. |

## What is included

- `include/vulkan/*.h`: the C API headers.
- `include/vk_video/*.h`: video codec headers that `vulkan_core.h` includes.
- `LICENSE.md` and `LICENSES/`.

## What was omitted

The engine uses the C API through volk and its own RHI types, so the C++ bindings and tooling are not vendored:

- `include/vulkan/*.hpp` and `include/vulkan/*.cppm` (Vulkan-Hpp C++ bindings, about 20 MB)
- `registry/`, `tests/`, `BUILD.gn`, `CMakeLists.txt`, `Makefile.release`, `REUSE.toml`, and the upstream README, contribution, conduct and security notes

`PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Licence election

Upstream offers `Apache-2.0 OR MIT`. Lykoware uses these headers under the MIT terms in `LICENSES/MIT.txt`. Both texts stay in the tree, unchanged.

## Integrity check

The vendored-file hash covers every upstream file in this directory, excluding `PROVENANCE.md`. From this directory:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```

## Update process

Replace this tree only from a pinned upstream tag, keep `third_party/volk` on the same Vulkan SDK tag, then update this record, the volk record, and the RHI together.
