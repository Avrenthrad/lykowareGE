# volk provenance

This directory holds the volk Vulkan meta-loader. It is not a Lykoware-owned library. Upstream copyright stays with Arseny Kapoulkine.

| Field | Value |
| --- | --- |
| Name | volk |
| Upstream | https://github.com/zeux/volk |
| Tag | `vulkan-sdk-1.4.363.0` (`VOLK_HEADER_VERSION` 363, matching `third_party/vulkan-headers`) |
| Commit | `54fc0d7a72887cbfc5b030d26897b2b40ac53fb8` (committed 2026-09-18) |
| Obtained by | `git clone --depth 1 --branch vulkan-sdk-1.4.363.0` of the upstream repository |
| Vendored-file sha256 | `ea6bdd0a9583d4c42e48ca5670630dfa4207950360b9e295a0db72345362f168` (see below) |
| SPDX licence | MIT |
| Licence file | [LICENSE.md](LICENSE.md) |
| Wrapper | `lykoware::rhi` in the engine library. Only `engine/src/rhi/` includes `volk.h`. |
| Local source patches | None. Present files are byte-identical to upstream. |

## What is included

`volk.c`, `volk.h` and `LICENSE.md`. Lykoware builds `volk.c` as the static target `lykoware_volk` in `cmake/VendorVulkan.cmake`, with `VK_NO_PROTOTYPES` and the vendored Vulkan-Headers.

## What was omitted

`CMakeLists.txt`, `cmake/`, `generate.py`, `test/` and the upstream README. Lykoware does not regenerate volk; a new version replaces both files from upstream.

`PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Runtime behaviour

volk opens the system Vulkan loader (`vulkan-1.dll` on Windows, `libvulkan.so.1` on Linux) at run time. There is no link-time Vulkan dependency. On Linux it links the platform `dl` library.

## Integrity check

The vendored-file hash covers every upstream file in this directory, excluding `PROVENANCE.md`. From this directory:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```
