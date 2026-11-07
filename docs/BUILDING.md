# Building lykowareGE

## Windows — primary target

Install:
- Visual Studio 2022 17.10 or newer, including **Desktop development with C++**, the MSVC v143 x64 toolset, and a Windows SDK.
- CMake 3.25 or newer, available on PATH.
- Python 3.10 or newer for integration tests (or configure with BUILD_TESTING=OFF).
- Git to obtain the repository.

From PowerShell:

```powershell
git clone https://github.com/Avrenthrad/lykowareGE.git
cd lykowareGE
cmake --preset windows
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows\samples\sandbox\Debug\lykoware_sandbox.exe
```

The current program prints startup messages and exits. It has no 3D graphics yet. `lykoware_sandbox --window` opens a window until it is closed. `lykoware_sandbox --window --dummy --frames N` uses the headless video driver and exits after N pumps.

For Release:

```powershell
cmake --build --preset windows-release
ctest --preset windows-release
cmake --install build/windows --config Release --prefix out/windows-release
.\out\windows-release\bin\lykoware_sandbox.exe
.\out\windows-release\bin\lykoware_sandbox.exe --version
```

Unknown options print an error and return a nonzero exit code. Use --help for usage.

The staged executable uses the Visual C++ runtime. A different Windows machine may need the Microsoft Visual C++ 2015–2022 x64 Redistributable. Debug builds require developer runtimes; distribute Release builds. Installer/runtime bundling will be addressed before playable distribution.

## Linux — secondary validation

With GCC 13+ (or another C++20 compiler supporting std::osyncstream), CMake 3.25+, Ninja, and Python 3.10+:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/samples/sandbox/lykoware_sandbox
```

Windows remains the primary target; this check helps keep the foundation portable.

## Shader tools (first configure)

Shaders are written in Slang and compiled to SPIR-V during the build ([ADR 0014](adr/0014-shader-build.md)). The first `cmake --preset ...` on a machine builds the Slang compiler and SPIRV-Tools from `third_party/` into `.lykoware-tools/`. That takes about 15 minutes on a 4-core machine, needs no network, and happens once per tool version; every preset then reuses it. Set `LYKOWARE_TOOL_CACHE` to share the cache between checkouts.

## Vulkan at run time

Building never needs a Vulkan SDK or driver: the Vulkan headers are vendored and the loader is opened at run time. To run the RHI tests instead of skipping them, a Vulkan loader must be installed. On Windows it comes with any current GPU driver (`vulkan-1.dll`). On Ubuntu:

```sh
sudo apt-get install libvulkan1 mesa-vulkan-drivers vulkan-validationlayers
```

`mesa-vulkan-drivers` includes lavapipe, a CPU Vulkan driver for machines without a GPU. Without a loader or a driver, the RHI tests are reported by CTest as Skipped, not Passed.

To see the Vulkan path on a desktop, run the sandbox with a window and Vulkan. It draws a red, green and blue triangle over a slowly changing background until the window is closed:

```powershell
.\build\windows\samples\sandbox\Debug\lykoware_sandbox.exe --window --vulkan
```

It exits 3 if no usable Vulkan device exists. `--window --offscreen --vulkan --frames N` is the headless form used by CTest.

The playable character is an original procedural training mannequin, GPU-skinned with seven joints and idle/walk clips (ADR 0052). Its pose follows the controller's speed, blends back to idle when stopped, and hides in first person. Animation is drawing-only; foot IK, ragdoll, finished character art and target-GPU budgets remain future work. It needs no downloaded model or external editor.

To walk a character round the Phase 2 obstacle course, add `--play`: WASD or the left stick to move, Shift or L3 to run, Space or South to jump, the mouse wheel or d-pad to zoom (in to first person). Click the window to look with the mouse, Esc to let it go, R to start again (ADR 0047):

```powershell
.\build\windows\samples\sandbox\Debug\lykoware_sandbox.exe --window --vulkan --play
```

The sandbox loads its shaders from the `shaders` folder beside its executable. The build puts them there, and `cmake --install` installs them to `bin/shaders`. If you copy the executable somewhere, copy that folder with it; without it the sandbox exits 1 and names the missing file (ADR 0016).

## Structure

- engine/: reusable static C++20 library, linked as lykoware::engine.
- samples/sandbox/: separate executable containing sample-game code.
- tools/cli/: native lyko executable, command dispatcher, and private platform process runner.
- tests/: sandbox smoke tests, engine unit tests, the RHI instance test, and CLI integration tests with genuine CMake/CTest runs.
- third_party/: vendored, pinned upstream sources (SDL3, Vulkan-Headers, volk, Vulkan Memory Allocator, and the host tools Slang, SPIRV-Tools and SPIRV-Headers) with provenance records.
- .lykoware-tools/ (not committed): host tools built from third_party on first configure and shared by every preset.
- assets/brand/: Lykoware logo vector set (SVG logo, icon, wordmark, mono variants), .ico, 512 px icon, preview sheet, BRAND.md guidelines and PROVENANCE.md.
- .github/workflows/build.yml: clean Windows Debug/Release builds, tests, staged execution, and downloadable artifacts; also Linux validation.

Build output is ignored by Git. CMakeUserPresets.json is available for local overrides.

## Dependencies and versions

SDL 3.4.18 is vendored under `third_party/sdl3` and built as a static library. The pin, zlib licence, HIDAPI BSD election, and omitted upstream directories are in [third_party/sdl3/PROVENANCE.md](../third_party/sdl3/PROVENANCE.md). The build does not download it. Logging still uses the C++ standard library. The project requires C++20 and CMake 3.25+; the presets use Visual Studio 2022 on Windows. CI actions are pinned to immutable commit SHAs in the workflow.

A Linux configure succeeds without X11 or Wayland headers. The dummy and offscreen drivers are enough for tests and for `--window --dummy`. A desktop `--window` on Linux needs X11 or Wayland development headers at configure time. `libx11-dev` and `libxext-dev` are enough for the X11 driver this build enables. Optional X11 extras are left off so a partial install does not fail configure. Windows builds the Win32 driver. Vulkan is not linked.

Evaluate and pin rendering, physics, and editor dependencies in their own modules. Do not download unversioned dependency branches during builds.

## Verification

CTest verifies the sandbox, the CPU trace API, the GPU-free image harness, window and input, and the CLI. `input.mapping` checks edge state and the SDL scancode, mouse, and gamepad map without a display. `window.lifecycle` creates, resizes, pumps, and closes a dummy window, including a virtual gamepad, focus changes, minimise, and platform close and quit events. `lifecycle.restart` shuts the window, audio, physics and ECS down and starts them again three times in one process; `lifecycle.restart_vulkan` does the same with the whole Vulkan stack (ADR 0040). `camera.follow` turns the third-person camera through 504 views in a cluttered room and checks it never sees through or from inside a wall (ADR 0044). `player.course` has a fixed-seed bot play the obstacle course through the same keyboard, mouse and gamepad path as a person, checking every checkpoint, the feet against the ground every tick and the camera every frame (ADR 0047). `scene.capture_golden` renders the falling-crates scene through `lyko screenshot` and compares it with `tests/golden/falling-crates-160x120.ppm` (software Vulkan only; ADR 0043). `scene.script` runs the falling-crates sample as a Lua script and checks it gives the command file's scene bit for bit, and that its recording replays (ADR 0042). `script.sandbox` checks the sandboxed Lua scripting API: what scripts can and cannot reach, the memory and instruction limits, host functions and repeatable results (ADR 0041). `foundation.smoke` also runs `lykoware_sandbox --window --dummy --frames 1`. GPU availability and skips are reported per test. `cli.commands` checks help text, `lyko run`, `lyko trace --frames`, and exit code 3 for `screenshot` and `run --scene`. `image.harness.api`, `image.harness.golden_pass`, and `image.harness.golden_fail` exercise the software pattern, committed PPM fixtures, and a comparison that is required to fail. Those tests are not renderer screenshots. Their outputs stay in the build directory. `perf.sandbox_startup` and `perf.cpu_fixture` enforce the provisional CPU-only thresholds in the root `CMakeLists.txt`. CLI integration covers real configure/build/test execution, literal Unicode/quoted arguments, child cwd, a Unicode trace path, failed configure/build/test commands, absent tools and zero-test failures. GitHub Actions repeats these on Windows Debug/Release, Linux Debug, ASan/UBSan, and clang-tidy 18 builds, then stages/runs the CLI. Current results are in STATUS.

## Native CLI

Bootstrap with CMake once, then use the executable:

```powershell
cmake --install build/windows --config Debug --prefix out/windows-debug
.\out\windows-debug\bin\lyko.exe build --source . --configure-preset windows --preset windows-debug
.\out\windows-debug\bin\lyko.exe test --source . --preset windows-debug
.\out\windows-debug\bin\lyko.exe run
```

On Linux:

```sh
./build/linux-debug/tools/cli/lyko build --source . --preset linux-debug
./build/linux-debug/tools/cli/lyko test --source . --preset linux-debug
./build/linux-debug/tools/cli/lyko run
```

See [CLI.md](CLI.md) for defaults, overrides, exit codes and the header contract. Installing the project stages both sandbox and lyko under bin/, which is where `lyko run` and `lyko trace` find the sandbox. `lyko trace --frames N` writes a CPU Chrome trace of the sandbox loop; `lyko trace --gpu --frames N` traces its Vulkan window with GPU frame times (ADR 0039). `screenshot` and `run --scene` exit 3 with `unsupported: no renderer`.

On Windows, use the installed copy when rebuilding the CLI itself: a running executable in the build directory cannot be relinked in place. Restage after the command exits to update the installed tool.

## Quality presets

Warnings are errors by default (/WX or -Werror). LYKOWARE_WARNINGS_AS_ERRORS=OFF is an explicit local escape hatch, not the CI configuration.

```sh
cmake --preset linux-sanitizers
cmake --build --preset linux-sanitizers
ctest --preset linux-sanitizers

# Install clang-tidy-18 first; its major version is fixed by the configuration.
cmake --preset linux-analysis
cmake --build --preset linux-analysis
ctest --preset linux-analysis
```

ASan/UBSan run on Linux GCC/Clang. Windows builds retain compiler warnings and integration tests; Windows sanitizer support is not configured. The .clang-tidy file enables analyzer/lifetime checks and treats findings as errors. ThreadSanitizer remains deferred until concurrent engine systems exist.

Python and analysis tools are development dependencies, not shipped game/runtime dependencies. The provisional CPU budgets are hosted-runner checks only. The image harness is not a screenshot gate, and no device-performance gate is claimed.

## CI artifacts

Builds, tests and staged execution are mandatory. Artifact upload is opt-in through the manual workflow's upload_artifacts input, defaults off, uses three-day retention, and is continue-on-error so a storage-quota rejection does not fail the foundation check. GitHub reported an exhausted artifact-storage quota during this task; no billing changes or artifact deletions were made. Ordinary PR/main checks do not require storage. Feature branches use PR checks rather than duplicate push/PR runs.

## First-build memory

Host shader tools default to two parallel build jobs, including on CPUs with many logical cores. Override with `-DLYKOWARE_HOST_TOOL_JOBS=N` (1–64) at configure time when machine memory permits. The local Windows validation runner explicitly uses two host-tool and two engine-build jobs for the owner's 16 GB PC. This is a concurrency limit, not a runtime performance setting; compiled tool options/recipe are unchanged. CI may restore prior cache contents, but HostTools selects only its exact version/recipe/platform directory.

### GPU skinning checks

`animation.mannequin` checks the procedural rig, inverse binds, sampled motion and blend timing. `render.gpu_skinning` compares real GPU captures to CPU-deformed meshes, including non-uniform scale, independent instances, palette reuse/growth/cap refusal, singular-normal fallback and renderer recreation. It also compares static geometry against the original pipeline. An unavailable Vulkan device reports Skipped, not a graphics pass.

After a native build, run `ctest --preset windows-debug -R "animation.mannequin|render.gpu_skinning|sandbox.play" --output-on-failure` (use `linux-debug` on Linux). Diagnostic PPMs, including `mannequin.ppm`, are under the build tree's `gpu-skinning/` directory. They are actual rendered test captures, not finished game art. Windows execution must still be performed on Windows; Linux software Vulkan is not that evidence.

The local verification in STATUS also includes a separate run with Khronos synchronization validation enabled. Install a development validation layer, set its loader/layer paths and `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`, then run the GPU skinning, Vulkan restart and sandbox play checks; confirm the layer's status log lists the synchronization feature. A normal passing capture without the layer does not establish validation-layer coverage. See ADR 0052 for the exact local package/configuration and results.

### Imported playable character

After building, run the sandbox with `--window --vulkan --play --character character.glb --idle-clip Idle --walk-clip Walk`. Selectors are exact, unique clip names; omitted selectors use first clip for idle and second (or first) for walk. Clips are required. Assets must be authored in metres, with feet at origin and forward -Z; capsule fitting and root motion are not implemented. The default procedural mannequin remains available by omitting `--character`. Test `sandbox.imported_player` generates an original minimal glTF and checks playback plus refusal paths.

Use `--character-height 1.8` with `--character` to fit the imported bind mesh visually to 1.8 metres, centre X/Z and put bind feet at the controller origin. Accepted target range is 0.1–10 metres. Without the flag authored placement is retained. The sandbox logs bind height, joint count and applied scale. This does not resize the physics capsule or correct forward orientation, and animated bounds can exceed bind bounds.

### Authored character profile

Instead of individual character flags, use `--window --vulkan --play --character-profile path/to/hero.character.json`:

```json
{"version":1,"model":"hero.glb","idle_clip":"Idle","walk_clip":"Walk","height":1.8}
```

The model path is relative to the profile folder; clip names and height are optional and follow the existing defaults. Do not mix a profile with individual character/clip/height flags. Use in-place locomotion clips: root-motion tracks currently affect the visual pose, not the controller. Bone-role mapping, retargeting and root-motion extraction are future work. Profiles are bounded to 64 KiB and shallow JSON; unknown and duplicate keys are errors.

### Humanoid leg roles

An optional `bones` object in a character profile identifies exact unique joint names:

```json
"bones": {"pelvis":"Hips", "left_hip":"LeftUpLeg", "left_knee":"LeftLeg", "left_foot":"LeftFoot", "right_hip":"RightUpLeg", "right_knee":"RightLeg", "right_foot":"RightFoot"}
```

All seven entries are required when the object is present. Names must resolve uniquely to distinct joints; each hip must descend from pelvis, knee from hip, and foot from knee. Intermediate helper joints are allowed. The sandbox prints resolved skeleton indices. Omit `bones` for non-humanoid or unmapped playback. This validates mapping/topology only: it does not enable IK, validate limb lengths, retarget motion or change physics.

### Two-bone IK geometry

`lykoware/ik.hpp` provides a drawing-space position solver with no stretching. It returns knee/end positions, reach-clamp and fallback flags. This does not apply bone rotations or plant feet. Run `ctest --preset linux-debug -R animation.two_bone_ik --output-on-failure` after building (use the corresponding Windows preset on Windows). Inputs share a double-precision coordinate space; lengths must be 1e-6..1e6 metres. Nonfinite inputs and outputs that cannot preserve limb lengths are refused without changing the previous solution. See ADR 0057 for scope.

`animation::pose_two_bone` now applies the position solution to a validated direct joint chain. It supports unit joint scales and fixed rest segment lengths, preserves local translations and unrelated rotations, and verifies FK before replacing outputs. Model-space target/hint inputs must be converted from actor/world coordinates by the caller; fitting offsets/scales must be included in that conversion. It is not connected to sandbox terrain yet. The current correctness-first implementation uses temporary allocations/full validation; do not assume an allocation-free crowd-animation path. `animation.two_bone_ik` covers both geometry and pose application.
