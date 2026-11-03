# ADR 0015 — Shader modules and the first graphics pipeline

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 5b (branch `claude/vulkan-rhi-5b`), stacked on RHI PR 5a.
Supersedes / superseded by: none. Builds on [ADR 0010](0010-rhi-device.md), [ADR 0011](0011-offscreen-render-target.md) and [ADR 0014](0014-shader-build.md).

## Context

PR 5a compiles Slang shaders to validated SPIR-V at build time. The RHI now needs to load them and draw. The ADR 0013 spike showed that Slang implements `SV_VertexID` with the SPIR-V `DrawParameters` capability, which the validation layer rejects unless the device enables Vulkan 1.1 `shaderDrawParameters`. Every renderer feature after this builds on the pipeline type, so the first version should be small, explicit and tested on real pixels.

## Options

- Expose raw Vulkan pipeline creation to callers. Leaks Vulkan types through the public API (ADR 0009 forbids that).
- A general pipeline description now: vertex formats, blending, depth, descriptors. Speculative before any caller needs them.
- A minimal pipeline type for what the first draw needs, extended field by field as callers appear.

## Decision

Proposed, not accepted by the owner:

- **`rhi::Shader`** wraps one `VkShaderModule` from SPIR-V words or a `.spv` file.
  - It checks the SPIR-V magic number and whole-word size. Full validation already happened at build time with `spirv-val`.
  - Every module exports its entry point as `main`, which is what Slang emits when it compiles one entry point per module (ADR 0014).
- **`rhi::GraphicsPipeline`** is built for dynamic rendering. It has:
  - one colour attachment, as `ColorFormat::Rgba8Unorm` or `Bgra8Unorm`;
  - triangle lists with no vertex buffers;
  - no culling, blending or depth;
  - dynamic viewport and scissor;
  - an empty pipeline layout.
  
  A missing shader or a shader of the wrong stage is `InvalidArgument`. Vertex input, descriptors, depth, blending and the driver-keyed pipeline cache (ADR 0002) are added when callers need them.
- **`RenderTarget::draw(clear, pipeline, vertex_count)`** records one pass, using the same dynamic-rendering helper that `clear()` and the swapchain use: load-op clear, bind, full-extent viewport and scissor, draw. A pipeline built for a different colour format is `InvalidArgument`, and nothing is submitted.
- **Device requirement:** Vulkan 1.1 `shaderDrawParameters` joins the ADR 0010 requirements and is enabled on every device. It is widely supported, including by lavapipe.
- **Sanitizer builds keep Vulkan drivers loaded.**
  - Under ASan, `rhi.pipeline` reported 112 bytes "leaked" in 2 allocations, with no symbols, from a thread Lykoware never creates.
  - With the lavapipe driver kept loaded, the report disappeared. It is memory lavapipe's LLVM compiler threads still hold when the Vulkan loader unloads the driver at `vkDestroyInstance`.
  - In sanitizer builds, the GPU tests therefore set the Khronos loader option `VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1`, which exists for leak checkers.
  - A deliberate leak of a `Shader` in the test was still reported with this option set, at `Shader::load` in `pipeline.cpp`. So the option hides the driver's shutdown noise, not Lykoware leaks.
- **Tests check sampled regions, not a stored image.** Rasterisation at triangle edges can differ between conformant GPUs, so `rhi.pipeline` samples points well inside each colour region and requires that drawing twice gives identical bytes. Stored reference images, with tolerances, come with real scenes.

## Contracts and compatibility

Ownership, lifetime, threading and failure are on the declarations in [engine/include/lykoware/rhi/pipeline.hpp](../../engine/include/lykoware/rhi/pipeline.hpp) and [render_target.hpp](../../engine/include/lykoware/rhi/render_target.hpp). A device lacking `shaderDrawParameters` is now unsuitable, with that reason. Pipelines do not keep their shaders; shaders can be destroyed once pipeline creation returns.

## Consequences

The engine draws real geometry headless, through the same pass code the window will use. How the runtime finds shader files (paths next to the executable, embedding or a packaged asset) is decided when the sandbox draws to the window (PR 5c).

## Verification

`rhi.pipeline` checks, with zero validation errors required:

- the build-time triangle shaders load;
- a pipeline is created and the shaders are released;
- the triangle draws into a 64×64 target and reads back through `RenderTargetCapture`;
- the background stays black, the regions near the three vertices are dominated by red, green and blue respectively, and the centre mixes all three;
- a second draw gives byte-identical output;
- drawing with a BGRA pipeline into the RGBA target is refused;
- non-SPIR-V data, a missing file, a vertex shader in the fragment slot, and a missing fragment shader are each refused.

Deliberate-break checks (all reverted, byte-identical):
- with the draw call removed, all four colour-region checks failed;
- with `shaderDrawParameters` not enabled, the validation layer reported `VUID-VkShaderModuleCreateInfo-pCode-08740` and the zero-errors check failed;
- with a `Shader` leaked under ASan, LeakSanitizer reported it with the loader option set.

Local results (Linux, GCC 13, lavapipe, validation layer on): 22/22 tests in the Debug, ASan/UBSan and clang-tidy presets. The sanitizer GPU tests passed three repeated runs. A `-Wshadow` sweep of the new and changed files is clean. CI results are recorded in STATUS when the PR runs.
