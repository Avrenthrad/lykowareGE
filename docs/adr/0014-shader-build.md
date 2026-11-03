# ADR 0014 — Host tools and the ahead-of-time shader build

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 5a (branch `claude/vulkan-rhi-5a`), merged as [#16](https://github.com/Avrenthrad/lykowareGE/pull/16) (merge c60264d).
Supersedes / superseded by: none. Implements the shader part of [ADR 0002](0002-graphics.md) with the compiler the owner chose in [ADR 0013](0013-shader-compiler.md).

## Context

Shaders are written in Slang and compiled ahead of time to SPIR-V (ADR 0002, ADR 0013). The compiler and SPIR-V tools must be built from vendored source with no downloads (ADR 0000). The ADR 0013 spike found that:

- `slangc` needs `-O0` because its own optimiser sits in the disabled `slang-glslang` part;
- a Slang build takes about 12 minutes on 4 cores;
- CI time matters, because the owner must approve any spend.

## Options

- Build Slang inside the engine's own build, as `add_subdirectory`. Every preset (Debug, sanitizers, clang-tidy) would rebuild it with its flags, which multiplies the cost and runs our warning and analysis gates on vendored code.
- Download prebuilt compilers. Rejected by ADR 0000.
- Build the tools once, in Release, as separate projects, into a cache keyed by tool versions that every preset and CI reuses.

## Decision

Proposed, not accepted by the owner:

- **Vendored, trimmed, unmodified:**
  - Slang v2026.18.3 (`third_party/slang`);
  - SPIRV-Tools v2026.4 (`third_party/spirv-tools`);
  - their shared SPIRV-Headers (`third_party/spirv-headers`).
  
  Licences, exact commits, omissions and hashes are in each `PROVENANCE.md`. No GPL file (lz4's programs) and no CC-BY content (cmark's spec, SPIRV-Headers' workspace file) is vendored. The single Unlicense file is the one the owner accepted.
- **`cmake/HostTools.cmake` builds `slangc`, `spirv-opt` and `spirv-val` at configure time:**
  - each as its own Release project, with its own flags and the engine's compiler;
  - into `LYKOWARE_TOOL_CACHE` (default `<source>/.lykoware-tools`, ignored by git);
  - under a key made of the tool versions, a recipe revision and the host OS and CPU. A file lock stops concurrent configures colliding, and a stamp file marks a complete build.
  
  Slang gets no network (prebuilt LLVM and DXC off), no glslang, and no optional targets. Its version is passed in, and it builds as one static executable. Slang's Vulkan-Headers dependency is pointed at our existing `third_party/vulkan-headers` through `cmake/host-tools/vulkan/`.
- **`cmake/Shaders.cmake` provides `lykoware_compile_shader`.** Each Slang entry point goes through:
  1. `slangc -O0 -warnings-as-errors all`, targeting SPIR-V 1.6;
  2. `spirv-opt -O` for Vulkan 1.3;
  3. `spirv-val`. The final `.spv` is copied into `<build>/shaders/` only after validation passes.
  
  SPIR-V is never committed.
- **CI caches `.lykoware-tools`** with `actions/cache` v6.1.0 (pinned SHA), keyed on the recipe file, the shim and the three provenance records. The owner approved the free Actions cache on 2026-11-03. Artifact storage is still not used.
- A test shader (`tests/shaders/triangle.slang`) is compiled on every build, and `shaders.spirv_valid*` re-validate its outputs in CTest. Drawing it is RHI PR 5b.

## Contracts and compatibility

- The first configure on a machine, or after a tool version changes, takes roughly 15 minutes longer. Later configures reuse the cache in seconds.
- Python 3.10+ (already a test prerequisite) is needed by SPIRV-Tools' generators.
- Changing a host-tool build option requires bumping `LYKOWARE_HOST_TOOLS_RECIPE`, so stale caches are not reused.
- Shader paths come from `lykoware_compile_shader`'s output variable. How the runtime loads shaders and pipelines (files, embedding, pipeline caches) is decided with the pipeline API in PR 5b.

## Consequences

- Every build proves its shaders compile, optimise and validate before any driver sees them.
- Our warning, sanitizer and clang-tidy gates do not cover Slang or SPIRV-Tools; they are vendored tools like SDL3.
- The repository grows by about 50 MB of vendored source.
- GitHub scopes a cache to the branch that saved it; other branches can read only their own, their base's and `main`'s. So the first run on `main` builds the tools once, and later branches restore `main`'s cache.

## Verification

- Local (Linux, GCC 13, network deliberately blocked):
  - the first configure built the host tools from the vendored trees in 689 s on 4 cores, and both triangle shaders compiled, optimised and validated;
  - 21/21 tests passed in Debug;
  - the sanitizer and clang-tidy presets reused the cache and passed 21/21.
- Getting there took three configure fixes, all now in the recipe: SPIRV-Tools needs `test/CMakeLists.txt` and `CHANGES`; Slang's SPIRV-Headers override names the parent folder; Slang always adds `examples/CMakeLists.txt`.
- CI: [run 41626480873](https://github.com/Avrenthrad/lykowareGE/actions/runs/41626480873) on b275474 passed all five jobs on the first, uncached run. Windows Debug and Release built the tools with MSVC (about 20 min per job, cold) and passed `shaders.spirv_valid*`, with the Vulkan tests Skipped. The Linux jobs (about 27 min, cold) passed with the RHI tests running on lavapipe. Two jobs on one OS share a cache key, so one may log that it could not reserve the cache while the other saves it; that is harmless.
