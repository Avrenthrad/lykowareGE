# SPIRV-Headers provenance

This directory holds the Khronos SPIR-V headers and grammar files. It is not a Lykoware-owned library. Upstream copyright stays with The Khronos Group Inc. It is shared by [Slang](../slang/PROVENANCE.md) and [SPIRV-Tools](../spirv-tools/PROVENANCE.md), which both pin this exact commit.

| Field | Value |
| --- | --- |
| Name | SPIRV-Headers |
| Upstream | https://github.com/KhronosGroup/SPIRV-Headers |
| Commit | `496543121ce6419f23d6fa5d7194ba66c36212d2` (the commit pinned by both Slang v2026.18.3 and SPIRV-Tools v2026.4) |
| Obtained by | Slang's `external/spirv-headers` submodule at that commit (`git submodule update --init --depth 1`) |
| Vendored-file sha256 | `0a98afe3badefc7f63e0387c84b72f6b0049ade195b688902f5368721271771e` (see below) |
| SPDX licence | MIT (Khronos), per [LICENSE](LICENSE) and [LICENSES/MIT.txt](LICENSES/MIT.txt) |
| Use | Build-time only: headers and grammar for the host tools `slangc`, `spirv-opt` and `spirv-val` (cmake/HostTools.cmake). |
| Local source patches | None. Present files are byte-identical to upstream. |

## Licence notes

Upstream `REUSE.toml` marks `WORKSPACE` and git metadata as CC-BY-4.0. Neither is vendored, so every vendored file is under the MIT terms in `LICENSE`. `LICENSES/CC-BY-4.0.txt` is kept unchanged as part of the upstream licence folder.

## What is included

`include/`, `cmake/`, `CMakeLists.txt`, `LICENSE` and `LICENSES/`.

## What was omitted

`tests/`, `tools/`, Bazel, GN and workspace files, `REUSE.toml`, and the README and contributor files. `PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Integrity check

The vendored-file hash covers every file in this directory except `PROVENANCE.md`. From this directory:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```

## Update process

Move only together with Slang and SPIRV-Tools, to the commit both of them pin, and update all three records, `LYKOWARE_HOST_TOOLS_RECIPE` and the CI cache key.
