# Vendored dependencies

| Directory | Version | Licence used | Wrapper | Record |
| --- | --- | --- | --- | --- |
| [sdl3/](sdl3/) | SDL 3.4.18 | Zlib (HIDAPI under its BSD option) | `lykoware::Window`, `lykoware::InputState` | [PROVENANCE](sdl3/PROVENANCE.md) |
| [vulkan-headers/](vulkan-headers/) | Vulkan-Headers `vulkan-sdk-1.4.363.0`, C headers only | MIT option of `Apache-2.0 OR MIT` | `lykoware::rhi` | [PROVENANCE](vulkan-headers/PROVENANCE.md) |
| [volk/](volk/) | volk `vulkan-sdk-1.4.363.0` | MIT | `lykoware::rhi` | [PROVENANCE](volk/PROVENANCE.md) |
| [vma/](vma/) | Vulkan Memory Allocator 3.4.0 | MIT | `lykoware::rhi` (device allocator) | [PROVENANCE](vma/PROVENANCE.md) |
| [slang/](slang/) | Slang v2026.18.3 (trimmed) | Apache-2.0 WITH LLVM-exception; one owner-accepted Unlicense file; bundled MIT/BSD-2 dependencies | Host tool `slangc` (build time only) | [PROVENANCE](slang/PROVENANCE.md) |
| [spirv-tools/](spirv-tools/) | SPIRV-Tools v2026.4 (trimmed) | Apache-2.0 | Host tools `spirv-opt`, `spirv-val` (build time only) | [PROVENANCE](spirv-tools/PROVENANCE.md) |
| [spirv-headers/](spirv-headers/) | SPIRV-Headers `4965431` | MIT | Shared by Slang and SPIRV-Tools | [PROVENANCE](spirv-headers/PROVENANCE.md) |
| [jolt/](jolt/) | Jolt Physics v5.6.0 (library, build file, licence) | MIT; two *Numerical Recipes*-derived headers replaced by Lykoware code (ADR 0003) | `lykoware::physics` | [PROVENANCE](jolt/PROVENANCE.md) |
| [json/](json/) | nlohmann/json 3.11.3 | MIT | private scene parser and CLI report encoding | [PROVENANCE](json/PROVENANCE.md) |
| [wuffs/](wuffs/) | Wuffs v0.4.0-alpha.12 (`wuffs-v0.4.c`; PNG and JPEG modules compiled) | MIT option of `MIT OR Apache-2.0` | `lykoware::decode_image` (ADR 0036) | [PROVENANCE](wuffs/PROVENANCE.md) |
| [lua/](lua/) | Lua 5.4.9 (`src/`, without the standalone programs; io, os, package and debug libraries not compiled) | MIT | `lykoware::script::Script` (ADR 0041) | [PROVENANCE](lua/PROVENANCE.md) |

Installed runtime attributions are indexed in [RUNTIME_NOTICES.md](RUNTIME_NOTICES.md) and verified by `package.runtime_notices`. Source vendoring alone does not carry these files into a copied binary package.

No other runtime source is vendored. Before adding a dependency, record:

- Name, upstream URL, exact source commit/version and integrity hash.
- Exact SPDX licence/terms, original copyright and required notices.
- Transitive/native dependencies and distribution obligations.
- Included source path, local patches, wrapper/API owner and update process.
- Build/runtime requirements and verification evidence.

Allowed source licences: MIT/BSD/Apache/zlib under [ADR 0000](../docs/adr/0000-ownership-and-licensing.md). Preserve upstream licence/NOTICE files inside each fork. Do not substitute the root proprietary notice for upstream terms. Existing CI action pins are workflow dependencies, not vendored runtime libraries; track their provenance separately.

The accepted runtime/native-tooling stack is C++20/CMake. Vendor native sources and transitive runtime dependencies rather than fetching moving branches during builds.

[ADR 0002](../docs/adr/0002-graphics.md) names Vulkan-Headers, the volk loader and Vulkan Memory Allocator as the graphics sources to pin when rendering work starts. Vulkan-Headers and volk are vendored with their first caller under [ADR 0009](../docs/adr/0009-vulkan-loading.md); Vulkan Memory Allocator with the first GPU allocation under [ADR 0011](../docs/adr/0011-offscreen-render-target.md). [ADR 0004](../docs/adr/0004-data-formats.md) allows a hand-written parser or pinned nlohmann/json and toml++ when a caller first needs one. Record exact versions and licences at that time. Do not add them in a documentation-only change. Physics was reviewed in [ADR 0003](../docs/adr/0003-physics.md) (owner accepted Jolt) and is built under [ADR 0018](../docs/adr/0018-jolt-integration.md). The shader compiler was reviewed in [ADR 0013](../docs/adr/0013-shader-compiler.md) (owner chose Slang) and is built as a host tool under [ADR 0014](../docs/adr/0014-shader-build.md); host tools run only at build time and are not shipped.
