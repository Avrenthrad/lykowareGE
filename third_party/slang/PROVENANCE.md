# Slang provenance

This directory is a trimmed copy of the Slang shading-language compiler. It is not a Lykoware-owned library. Upstream copyright stays with the Slang authors and the authors of each bundled dependency. Owner decision: [ADR 0013](../../docs/adr/0013-shader-compiler.md).

| Field | Value |
| --- | --- |
| Name | Slang |
| Upstream | https://github.com/shader-slang/slang |
| Tag | `v2026.18.3` (released 2026-09-24) |
| Commit | `6eb89786ca882d71049c8568638e247f60864b6f` |
| Obtained by | `git clone --depth 1 --branch v2026.18.3`, then `git submodule update --init --depth 1` for the dependencies listed below only |
| Vendored-file sha256 | `9e0642e508afae5132bf041bfb90a8893231019f1aae923d4ce1da3815887001` (see below) |
| SPDX licence | `Apache-2.0 WITH LLVM-exception` (Slang's own code); see exceptions and dependencies below |
| Licence file | [LICENSE](LICENSE), full texts in [LICENSES/](LICENSES/), per-file annotations in [REUSE.toml](REUSE.toml) |
| Use | Host tool only: `slangc` compiles shaders to SPIR-V at build time. Not linked into the engine and not shipped in games. Built by [cmake/HostTools.cmake](../../cmake/HostTools.cmake). |
| Local source patches | None. Present files are byte-identical to upstream. |

## Licence exceptions inside Slang (from upstream REUSE.toml)

- `source/core/slang-crypto.cpp`: **Unlicense** (public domain). It is compiled. **Accepted by the owner on 2026-11-03** as a named exception to ADR 0000.
- `external/dxc/dxcapi.h` (NCSA) and `tools/gfx-unit-test/span.h` (BSL-1.0): `external/dxc` is not vendored, and the gfx unit test is not built.

## Bundled dependencies (Slang submodules at the commits Slang pins)

| Path | Upstream | Commit | Licence | Compiled into |
| --- | --- | --- | --- | --- |
| `external/unordered_dense` | martinus/unordered_dense | `73f3cbb237e84d483afafc743f1f14ec53e12314` | MIT | `slangc` and generators |
| `external/miniz` | richgel999/miniz | `6ef6c68f4fcbb8287aa8edf9c6670804932f41c6` | MIT (RAD Game Tools, Valve, Rich Geldreich) | `slangc` |
| `external/lz4` (`lib/` and `build/cmake/` only) | lz4/lz4 | `7f71be01f2c6f6b2c3cddb67eef8c38eab12ffd4` | BSD-2-Clause for `lib/` | `slangc` |
| `external/cmark` | swiftlang/swift-cmark | `924936d0427cb25a61169739a7660230bffa6ea6` | BSD-2-Clause with MIT notices (see its COPYING) | `slangc` |
| `external/fast_float` (`include/` and licence files) | fastfloat/fast_float | `e0b53eaf63c6d00e0725788ef1dbb759aa321d79` | Apache-2.0 OR MIT OR BSL-1.0; Lykoware uses it under Apache-2.0 | `slangc` |
| `external/lua` | lua/lua | `3fe7be956f23385aa1950dc31e2f25127ccfc0ea` | MIT (notice at the end of `lua.h`) | build-time generator `slang-fiddle` only, not in `slangc` |
| `external/slang-tint-headers` | part of the Slang tree | (Slang commit) | Apache-2.0 WITH LLVM-exception | header only |

SPIRV-Headers is shared with SPIRV-Tools from [../spirv-headers](../spirv-headers/PROVENANCE.md), at the same commit Slang pins (`496543121ce6`). Vulkan headers come from [../vulkan-headers](../vulkan-headers/PROVENANCE.md) through the shim in `cmake/host-tools/vulkan/`.

## What was omitted

From Slang: `docs/`, `tests/`, `examples/`, `extras/`, `docker/`, `.github/`, `.claude/`, and top-level README, contributor, review, agent (`AGENTS.md`, `CLAUDE.md`), editor and Nix files. Every submodule not listed above is omitted, including glslang, SPIRV-Tools, DXC, slang-rhi, OptiX, imgui, glm, Catch2, replxx, mimalloc, metal-cpp, nlohmann-json, tinyobjloader and Slang's own Vulkan-Headers copy. The build switches them off (ADR 0013).

From dependencies:
- lz4: everything except `lib/`, `build/cmake/` and `LICENSE`. This excludes lz4's GPL-2.0-or-later programs, so no GPL file is vendored.
- unordered_dense: `data/`, `test/`, `doc/`, `scripts/` and `subprojects/`.
- lua: `testes/` and `manual/`.
- fast_float: `tests/`, `docs/` and `script/`.
- cmark: `bench/` and `test/`. This removes `test/spec.txt`, the only cmark file under CC-BY-SA 4.0.
- miniz: `tests/` (including a BSL-1.0 Catch2 copy) and `examples/`. Neither is built when miniz is a subproject.

`PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Build-time behaviour

`cmake/HostTools.cmake` configures this tree as a separate Release project with no network access. The prebuilt LLVM and DXC downloads are off (`SLANG_SLANG_LLVM_FLAVOR=DISABLE`, `SLANG_ENABLE_DXIL=OFF`), and glslang, GFX, tests, examples, CUDA and OptiX are off. The version is passed in (`SLANG_VERSION_FULL`) because the tree has no git history. Only `slangc` is built, as a single static executable.

## Integrity check

The vendored-file hash covers every file in this directory except `PROVENANCE.md`. From this directory:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```

## Update process

Replace this tree from a pinned Slang release and its pinned submodules, apply the same omissions, then update this record, `LYKOWARE_SLANG_VERSION` and `LYKOWARE_HOST_TOOLS_RECIPE` in `cmake/HostTools.cmake`, and the CI cache key together.
