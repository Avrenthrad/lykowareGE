# SPIRV-Tools provenance

This directory holds the Khronos SPIR-V optimiser and validator sources. It is not a Lykoware-owned library. Upstream copyright stays with The Khronos Group Inc. and the SPIRV-Tools authors.

| Field | Value |
| --- | --- |
| Name | SPIRV-Tools |
| Upstream | https://github.com/KhronosGroup/SPIRV-Tools |
| Tag | `v2026.4` (released 2026-09-10) |
| Commit | `ef96ed763b43b59b33b31b362f09a02b729fa1c9` |
| Obtained by | `git clone --depth 1 --branch v2026.4` of the upstream repository |
| Vendored-file sha256 | `9d3b3b5776cddcb62d7ecbe636880e71b659d8a0b508c30d5ef9471e1ac136db` (see below) |
| SPDX licence | Apache-2.0 |
| Licence file | [LICENSE](LICENSE) |
| Use | Host tools only: `spirv-opt` optimises and `spirv-val` validates every shader at build time (cmake/Shaders.cmake). Not linked into the engine and not shipped. |
| Local source patches | None. Present files are byte-identical to upstream. |

SPIRV-Headers comes from [../spirv-headers](../spirv-headers/PROVENANCE.md) at the commit this release pins (`496543121ce6`), passed as `SPIRV-Headers_SOURCE_DIR`.

## What is included

`source/`, `include/`, `tools/`, `utils/` (Python generators run during the build; Python 3.10+ is already a test prerequisite), `examples/`, `cmake/`, `external/CMakeLists.txt`, `test/CMakeLists.txt` (which returns at once when `SPIRV_SKIP_TESTS` is on), `CMakeLists.txt`, `CHANGES` (read to generate the version string) and `LICENSE`.

## What was omitted

The rest of `test/` (about 18 MB), `docs/`, `kokoro/`, `android_test/`, `build_overrides/`, Bazel, GN, Android and presubmit files, and the README and contributor files. `PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Integrity check

The vendored-file hash covers every file in this directory except `PROVENANCE.md`. From this directory:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```

## Update process

Move together with SPIRV-Headers to the commit this release pins. Update this record, `LYKOWARE_SPIRV_TOOLS_VERSION` and `LYKOWARE_HOST_TOOLS_RECIPE` in `cmake/HostTools.cmake`, and the CI cache key.
