# ADR 0013 — Shader compiler (licence review and choice)

Status: **Accepted by the owner on 2026-11-03: Slang**, conditional on the build spike below. Nothing is vendored until the spike passes.
Related task/PR: Vulkan RHI bring-up, PR 5 prerequisite (branch `claude/shader-compiler-adr`), stacked on RHI PR 4.
Supersedes / superseded by: none. Required by [ADR 0002](0002-graphics.md): "The shader-compiler licence must be reviewed as a build-time tool before that compiler is adopted."

## Context

The RHI can clear and present but cannot draw: pipelines need shaders compiled ahead of time to SPIR-V (ADR 0002, brief section 8). [ADR 0000](0000-ownership-and-licensing.md) allows only MIT, BSD, Apache and zlib low-level code, vendored as source, with no copyleft in shipped code. A shader compiler starts as a build-time tool, but the editor (Track E) and agent tooling will want hot reload, so the compiler is likely to ship inside Lykoware tools later. Its licence therefore matters as if it ships.

This review reads the licence files and the REUSE/SPDX annotations of each candidate at its latest stable release. It is a technical licence inventory, not legal advice. Anything outside the ADR 0000 list needs the owner's decision and, if the owner wants it, legal review.

## Evidence (fetched 2026-11-03)

| Candidate | Release (date) | Commit | What it is |
| --- | --- | --- | --- |
| glslang | 16.6.0 (2026-09-11) | `e1b562a8bed2` | Khronos reference GLSL (and HLSL) front end to SPIR-V |
| shaderc | v2026.4 (2026-09-11) | `b64fd42c4a2a` | Google wrapper around glslang and SPIRV-Tools |
| SPIRV-Tools | v2026.4 (2026-09-10) | `ef96ed763b43` | Khronos SPIR-V validator, optimiser, disassembler (not a compiler) |
| DirectX Shader Compiler (DXC) | v1.10.2605.37 (2026-08-06) | `c4d8f4f99aa2` | Microsoft HLSL compiler on an LLVM 3.7 fork, with a SPIR-V back end |
| Slang | v2026.18.3 (2026-09-24) | `6eb89786ca88` | Shading language and compiler from the shader-slang project (modules, generics, interfaces, automatic differentiation); emits SPIR-V, DXIL, Metal and more |

Licence findings:

- **glslang.**
  - Core parsing and SPIR-V generation are BSD-3-Clause, MIT and Apache-2.0.
  - `glslang/MachineIndependent/glslang_tab.cpp` and `glslang_tab.cpp.h` are Bison output, licensed **GPL-3.0-or-later WITH Bison-exception-2.2**, and are compiled into glslang proper. The Bison exception lets a larger work containing the skeleton be distributed "under terms of your choice", provided the work is not itself a parser generator.
  - The preprocessor (`MachineIndependent/preprocessor/Pp*.cpp`) carries an additional custom NVIDIA licence (SPDX `AML-glslang`). It is permissive in effect, but it is not MIT, BSD, Apache or zlib.
  - Upstream's `LICENSE.txt` says the Bison files can be regenerated "using tools of your choice", so replacing the generated parser would mean writing or generating our own.
- **shaderc.** Apache-2.0 itself, but it builds the same glslang commit (`e1b562a8`), so it inherits glslang's GPL-with-exception and AML files. It adds abseil, re2 and effcee for tests. It brings no licence advantage over glslang.
- **SPIRV-Tools.** Apache-2.0 throughout; no GPL text in `source/` or `include/`. Useful whatever compiler is chosen: `spirv-val` checks compiler output in CI.
- **DXC.**
  - The licence is the LLVM Release Licence (**University of Illinois/NCSA**), which is permissive and BSD-like, but not one of the four names in ADR 0000.
  - `LICENSE.TXT` also reproduces the Autoconf GPL exception text for configure-script material.
  - It is a large LLVM fork: heavy to vendor and build.
  - Its primary output is DXIL; SPIR-V is an additional back end.
- **Slang.**
  - **Apache-2.0 WITH LLVM-exception**, which falls under Apache.
  - Its REUSE file lists three exceptions inside the Slang tree:
    - `source/core/slang-crypto.cpp` is **Unlicense**, a public-domain dedication. It is in core source, so it is likely compiled;
    - `external/dxc/dxcapi.h` is **NCSA**. It is only needed for DXIL output, which can be turned off;
    - `tools/gfx-unit-test/span.h` is **BSL-1.0**. It is a test tool and need not be built.
  - glslang **and** SPIRV-Tools come in only through the optional `slang-glslang` target. Setting `SLANG_ENABLE_SLANG_GLSLANG=OFF` leaves glslang's GPL-with-exception and AML files out entirely.
  - Other submodules (OptiX, slang-rhi, imgui, glm, Catch2, replxx and others) appear to belong to optional targets: CUDA/OptiX, tests, examples and the REPL. `CMakeLists.txt` has switches for those targets. Which submodules a minimal build still compiles is unverified.

## Options

1. **glslang** (GLSL). Mature, the smallest of the real compilers, the Khronos reference. Needs the owner to accept a GPL-with-Bison-exception parser and the NVIDIA AML preprocessor licence, or to replace both.
2. **shaderc.** Same licence position as glslang plus extra dependencies. No reason to prefer it.
3. **DXC** (HLSL). Industry-standard HLSL. Needs the owner to accept NCSA. Very large build, and SPIR-V is not its primary output.
4. **Slang, built without `slang-glslang`, plus SPIRV-Tools vendored separately for validation.** Apache family throughout apart from one Unlicense file (and NCSA/BSL files that need not be built). It offers the most for an engine aiming above AAA: modules and interfaces suit a material and shader-permutation system, it emits SPIR-V and DXIL from one source (useful if a D3D12 back end is ever justified), and automatic differentiation helps later rendering and simulation research. It is a large code base, and its build cost and minimal configuration are unmeasured.
5. **Use prebuilt compiler binaries** (for example from the Vulkan SDK). Rejected: it conflicts with ADR 0000's vendored-source rule.
6. **Write a Lykoware shader compiler.** Rejected for now: years of work before Phase 1 can draw. It could be revisited for the content suite.

## Decision

**Owner decision, 2026-11-03.** Joshua chose Slang, accepted the single Unlicense file in Slang core (`source/core/slang-crypto.cpp`) as an exception to the ADR 0000 licence list, and asked for the spike to run. The shader language is therefore Slang. The record of the original recommendation follows.

The recommendation is option 4: **Slang** with `SLANG_ENABLE_SLANG_GLSLANG=OFF`, DXIL, CUDA, OptiX, tests, examples, the REPL and GFX off, plus **SPIRV-Tools** (Apache-2.0) vendored for `spirv-val` in CI. Before anything is vendored, a spike must show:

1. A minimal Slang configuration builds from vendored source on Windows (MSVC) and Linux (GCC) with our warning and sanitizer presets. Record the build time and the vendored size.
2. Slang emits SPIR-V **without** `slang-glslang`.
3. A triangle shader compiled ahead of time passes `spirv-val`, runs on lavapipe with zero validation errors, and matches an image-harness reference.
4. The exact list of compiled source files, with their licences and notices. That includes every submodule the minimal configuration actually builds.

If the spike fails item 1 or 2, the fallback is option 1 (glslang). That fallback needs the owner's explicit acceptance of the Bison-exception and AML terms, or a plan to replace the generated parser and preprocessor.

**Owner answers (2026-11-03):**

- The Unlicense file inside Slang core: **accepted**, as a named exception to ADR 0000's list. Any further non-listed licence still needs the owner.
- Slang or glslang: **Slang**. The glslang licence questions do not arise unless the spike fails and the fallback is needed. They would then go back to the owner.
- Shader language: **Slang**.

## Spike results (2026-11-03, Linux)

Run in a scratch directory; nothing was committed or vendored. Slang v2026.18.3 (`6eb89786ca88`), SPIRV-Tools v2026.4 (`ef96ed763b43`), Mesa 25.2 lavapipe, Khronos validation layer.

1. **Minimal build from source, offline. Passed on Linux.**
   - Configured with:
     - `SLANG_ENABLE_SLANG_GLSLANG=OFF`, `SLANG_ENABLE_DXIL=OFF`, `SLANG_SLANG_LLVM_FLAVOR=DISABLE`;
     - GFX, slang-rhi, tests, examples, slangd, slangi, slang-rt, CUDA, OptiX, NVAPI, mimalloc and the replayer all off.
   - Configure and build ran with the network deliberately blocked, so nothing was downloaded. Two settings matter here: the default `SLANG_SLANG_LLVM_FLAVOR` downloads a prebuilt LLVM, and `SLANG_ENABLE_DXIL` downloads a prebuilt DXC. Both must stay off.
   - The `slangc` Release build took **732 s on 4 cores** (491 build steps). SPIRV-Tools (`spirv-val`, `spirv-opt`) took **191 s**.
   - Not verified: Windows/MSVC (CI will be the first check), and Slang under our sanitizer and clang-tidy presets. Slang is vendored code, so like SDL3 and volk it is built with its own flags.
2. **SPIR-V without `slang-glslang`. Passed, with one condition.**
   - By default `slangc` passes its SPIR-V through `spirv-opt`, which it loads from the disabled `slang-glslang` library, so compilation fails.
   - With **`-O0`** it emits SPIR-V 1.6 directly.
   - Optimisation is kept by running **our own** vendored `spirv-opt -O` as a separate build step, then `spirv-val`.
3. **Triangle. Passed.**
   - A Slang vertex and fragment shader (`SV_VertexID`, interpolated colour) compiled at `-O0` passed `spirv-val --target-env vulkan1.3`. After `spirv-opt -O` it passed again (vertex shader 1468 → 1084 bytes).
   - Drawn with dynamic rendering on lavapipe, both versions produced **byte-identical images**, with every pixel check passing.
   - The validation layer flagged one real requirement: Slang implements `SV_VertexID` with the SPIR-V `DrawParameters` capability, so the device must enable the Vulkan 1.1 feature **`shaderDrawParameters`**. With it enabled, there were **zero validation errors**.
4. **Compiled-file inventory. Passed.** From Ninja's dependency records for every object built:
   - Slang's own source: `source/core`, `compiler-core`, `slang`, `slang-core-module`, `slang-glsl-module` (Slang's own GLSL-compatibility module, not glslang), `slang-record-replay`, `slangc`, and generator tools. All Apache-2.0 WITH LLVM-exception, except the accepted Unlicense file `source/core/slang-crypto.cpp`.
   - External, all permissive:
     - `lz4` (only `lib/`, BSD-2-Clause; none of lz4's GPL-2.0 program files are compiled);
     - `miniz` (MIT-style);
     - `cmark` (BSD-2-Clause and MIT notices);
     - `fast_float` (Apache-2.0 / MIT / BSL-1.0, our choice);
     - `unordered_dense` (MIT);
     - `spirv-headers` (Khronos MIT);
     - `lua` (MIT), used only by the build-time generator `slang-fiddle`, so it is not in shipped binaries.
   - **No file** from `external/glslang`, `external/spirv-tools`, DXC or LLVM was compiled. The only files under `source/slang-glslang` and `source/slang-llvm` were Slang's own interface headers.
   - SPIRV-Tools v2026.4 pins SPIRV-Headers `4965431`, the same commit Slang pins, so one vendored copy serves both.

**Costs found by the spike (owner awareness):**

- **Repository size.** The minimal Slang source set (source, include, tools, cmake, prelude) is about 32 MB, plus the external parts above. Slang's own `external/vulkan` (which includes the large Vulkan-Hpp) can be replaced by our existing `third_party/vulkan-headers` through Slang's override option.
- **CI time.** Building `slangc` in all five CI jobs on every push would add roughly 12 to 25 minutes per job; GitHub's hosted runners for private repositories have fewer cores than this machine. Windows minutes count double against the plan's monthly allowance. Planned mitigation: cache the built compiler per Slang commit with the GitHub Actions cache. That cache is a free per-repository allowance (10 GB), separate from the exhausted artifact storage. **Owner confirmation requested before relying on it**, because the owner's rule forbids paid CI storage.

## Contracts and compatibility

No API or format changes here. When a compiler is adopted:

- shaders are authored as text in the repository;
- SPIR-V is a build output, never committed (owner rule: no regenerable assets);
- the compiler is wrapped behind a Lykoware build step and, later, a tool API, so it can be replaced;
- the vendored compiler's exact licence inventory goes in its `PROVENANCE.md`.

## Consequences

Drawing anything beyond clears waits for this decision and the spike. Choosing Slang commits shader authors to the Slang language (close to HLSL). Choosing glslang commits them to GLSL, and adopts the licence terms above. Either way, SPIRV-Tools validation in CI catches compiler output errors before the driver sees them.

## Verification

- Licence evidence comes from upstream `LICENSE`, `LICENSES/`, `REUSE.toml`, `.gitmodules`, `ThirdPartyNotices.txt` and `CMakeLists.txt` files at the commits above, read on 2026-11-03.
- Spike (above): items 1 to 4 passed on Linux. Not verified: a Windows/MSVC build of Slang, and Slang under our sanitizer and clang-tidy presets.
- This record ticks no checklist box.
