# Vendored dependencies

SDL 3.4.18 is vendored under [sdl3/](sdl3/) and wrapped by the engine window and input API. Provenance, the zlib licence, the elected HIDAPI BSD terms, and the omitted archive directories are recorded in [sdl3/PROVENANCE.md](sdl3/PROVENANCE.md). No other runtime source is vendored. Before adding a dependency, record:

- Name, upstream URL, exact source commit/version and integrity hash.
- Exact SPDX licence/terms, original copyright and required notices.
- Transitive/native dependencies and distribution obligations.
- Included source path, local patches, wrapper/API owner and update process.
- Build/runtime requirements and verification evidence.

Allowed source licences: MIT/BSD/Apache/zlib under [ADR 0000](../docs/adr/0000-ownership-and-licensing.md). Preserve upstream licence/NOTICE files inside each fork. Do not substitute the root proprietary notice for upstream terms. Existing CI action pins are workflow dependencies, not vendored runtime libraries; track their provenance separately.

The accepted runtime/native-tooling stack is C++20/CMake. Vendor native sources and transitive runtime dependencies rather than fetching moving branches during builds.

[ADR 0002](../docs/adr/0002-graphics.md) names Vulkan-Headers, the volk loader and Vulkan Memory Allocator as the graphics sources to pin when rendering work starts. [ADR 0004](../docs/adr/0004-data-formats.md) allows a hand-written parser or pinned nlohmann/json and toml++ when a caller first needs one. Record exact versions and licences at that time. Do not add them in a documentation-only change. The shader-compiler licence is a separate build-time review.
