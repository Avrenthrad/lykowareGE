# ADR 0016 — How a program finds its shaders at run time

Status: Proposed
Related task/PR: Vulkan RHI bring-up, PR 5c (branch `claude/vulkan-rhi-5c`), based on `main` after RHI PR 5b ([#17](https://github.com/Avrenthrad/lykowareGE/pull/17)) merged.
Supersedes / superseded by: none. Builds on [ADR 0014](0014-shader-build.md) and [ADR 0015](0015-graphics-pipeline.md).

## Context

Shaders are compiled to validated SPIR-V at build time (ADR 0014), and tests have been given the `.spv` paths on their command line. A real program, starting with `lykoware_sandbox --window --vulkan`, has no such arguments. It must find its shaders in the build tree, in a `cmake --install` staging folder (CI already runs the staged executables), and later in a shipped build, wherever it is launched from. An asset system arrives later in Phase 1; this decision covers only the build-time compiled shaders a program ships with until then.

## Options

- **Embed the SPIR-V in the executable** as generated C++ arrays. Nothing to find or install. But every shader change relinks the program, generated sources join the build, and the approach does not carry over to assets, hot reload or packaged content.
- **Look relative to the current working directory.** Simple, but it breaks as soon as the program is started from another folder, a shortcut or the CLI.
- **A path from the command line or an environment variable.** Useful for tools, but every launch has to supply it, and it can still point at stale files.
- **A `shaders` folder beside the executable**, found from the executable's own location. Works in the build tree, in a staged install and in a shipped folder; uses ordinary files like the assets that will follow.

## Decision

Proposed, not accepted by the owner:

- **Location.** A program loads its compiled shaders from `lykoware::shader_directory()`, which is `executable_directory() / "shaders"`.
  - `executable_directory()` comes from SDL's `SDL_GetBasePath()`, which is already linked, so no new dependency.
  - The current working directory is never used.
- **Deployment.** `lykoware_deploy_shaders(<target> INSTALL_DESTINATION <dir> SHADERS ...)` in `cmake/Shaders.cmake`:
  - copies the validated `.spv` files into the `shaders` folder beside the target's executable on every build, per configuration with multi-config generators;
  - installs them to `<dir>/shaders`.
  - It uses `copy_if_different`, so unchanged files are untouched. CMake cannot name a target's output folder in a custom-command output, so the copy step runs on every build; it costs one small copy and also restores a deleted folder.
- **Reading before the GPU.** `rhi::read_spirv_file()` reads and checks a `.spv` file without a `Device`. The sandbox reads its shaders before it opens a window or touches Vulkan, so a missing or damaged install fails with exit 1 and a message naming the file, even on a machine with no Vulkan driver.
- **Names.** Shader file names are unique per program. The sandbox's are prefixed `sandbox_`. They first came from its own source, `samples/sandbox/shaders/triangle.slang`; since [ADR 0026](0026-renderer.md) they are the engine's lit shader, `engine/shaders/lit.slang`, deployed as `sandbox_lit.*`. The test fixture `tests/shaders/triangle.slang` stays separate, so the sample can change without changing what tests check.
- **Later.** When the asset system arrives, it may move shaders into its content root or a package. That change will get its own ADR.

## Contracts and compatibility

- New public API:
  - `lykoware/paths.hpp`: `executable_directory()` and `shader_directory()`;
  - `lykoware/rhi/pipeline.hpp`: `read_spirv_file()`.
- `Shader::load_file()` now reads through `read_spirv_file()`. Its errors are unchanged, apart from the not-SPIR-V case now naming the file.
- `lykoware_sandbox --window --vulkan`:
  - draws the triangle over the cycling clear colour;
  - exits 1 when its shaders are missing, before opening a window;
  - exit 3 (no usable Vulkan) and the validation-error exit 1 are unchanged.
- Swapchain additions, recorded here because the sandbox is their first caller:
  - `Swapchain::color_format()` and `Swapchain::render_draw()`;
  - `create()` now refuses, with `NoSuitableDevice`, a surface that offers no 8-bit UNORM BGRA or RGBA format, so every swapchain has a format a pipeline can target.

## Consequences

- A program and its shaders form one folder that can be moved or staged as a unit. The CI staging steps prove the install contains them.
- Programs must keep their shader file names unique, and must ship the `shaders` folder with the executable.
- One extra copy step per program on every build.
- The triangle's pixels in a real window are not checked automatically. A swapchain image is not read back; the same draw pass is pixel-checked offscreen by `rhi.pipeline`.

## Verification

- `sandbox.vulkan_window` runs the build-tree sandbox, which loads its deployed shaders and draws 3 frames with the validation layer on.
- `sandbox.missing_shaders` copies the sandbox to a folder without `shaders` and requires:
  - exit 1;
  - a message naming `sandbox_triangle.vert.spv`;
  - no window opened.
  
  It runs everywhere, including machines without Vulkan.
- `rhi.swapchain` now builds the test triangle pipeline for the swapchain's format and draws 4 frames through `render_draw`. It also requires a pipeline for the other format to be refused with nothing presented, and zero validation errors.
- CI runs the installed (staged) sandbox:
  - on Linux it must exit 0 after drawing on lavapipe;
  - on Windows it must exit 3, which shows the shaders were found and only Vulkan is missing. A missing install would exit 1.
- Local (Linux, GCC 13, lavapipe, validation layer on):
  - 23/23 tests in Debug;
  - the installed sandbox ran from `/` as working directory and drew 3 frames, and exited 1 with the file named once its `shaders` folder was renamed.
- Deliberate breaks, each caught:
  - removing the format check in `render_draw` made `rhi.swapchain` fail, with VUID-vkCmdDraw-dynamicRenderingUnusedAttachments-08910;
  - destroying the pipeline before the swapchain made `sandbox.vulkan_window` fail, with VUID-vkDestroyPipeline-pipeline-00765.
- CI: [run 41747802339](https://github.com/Avrenthrad/lykowareGE/actions/runs/41747802339) on e9fc338 passed all five jobs.
  - Linux: 23/23 in each preset, and the installed sandbox drew 3 frames on lavapipe (also in the sanitizer build).
  - Windows Debug and Release: `sandbox.missing_shaders` passed; the install contained `bin/shaders/`; the installed sandbox exited 3 for lack of Vulkan, after finding its shaders.
- Not verified: a visible desktop window on a real GPU. Owner check: run `lykoware_sandbox --window --vulkan` on the RTX 4070 and the MSI Claw. A red, green and blue triangle should sit over a slowly changing background.
