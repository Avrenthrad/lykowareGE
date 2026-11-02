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

## Vulkan at run time

Building never needs a Vulkan SDK or driver: the Vulkan headers are vendored and the loader is opened at run time. To run the RHI tests instead of skipping them, a Vulkan loader must be installed. On Windows it comes with any current GPU driver (`vulkan-1.dll`). On Ubuntu:

```sh
sudo apt-get install libvulkan1 mesa-vulkan-drivers vulkan-validationlayers
```

`mesa-vulkan-drivers` includes lavapipe, a CPU Vulkan driver for machines without a GPU. Without a loader or a driver, the RHI tests are reported by CTest as Skipped, not Passed.

To see the Vulkan path on a desktop, run the sandbox with a window and Vulkan; it shows a window slowly cycling through colours until it is closed:

```powershell
.\build\windows\samples\sandbox\Debug\lykoware_sandbox.exe --window --vulkan
```

It exits 3 if no usable Vulkan device exists. `--window --offscreen --vulkan --frames N` is the headless form used by CTest.

## Structure

- engine/: reusable static C++20 library, linked as lykoware::engine.
- samples/sandbox/: separate executable containing sample-game code.
- tools/cli/: native lyko executable, command dispatcher, and private platform process runner.
- tests/: sandbox smoke tests, engine unit tests, the RHI instance test, and CLI integration tests with genuine CMake/CTest runs.
- third_party/: vendored, pinned upstream sources (SDL3, Vulkan-Headers, volk, Vulkan Memory Allocator) with provenance records.
- assets/brand/: Lykoware logo vector set (SVG logo, icon, wordmark, mono variants), .ico, 512 px icon, preview sheet, BRAND.md guidelines and PROVENANCE.md.
- .github/workflows/build.yml: clean Windows Debug/Release builds, tests, staged execution, and downloadable artifacts; also Linux validation.

Build output is ignored by Git. CMakeUserPresets.json is available for local overrides.

## Dependencies and versions

SDL 3.4.18 is vendored under `third_party/sdl3` and built as a static library. The pin, zlib licence, HIDAPI BSD election, and omitted upstream directories are in [third_party/sdl3/PROVENANCE.md](../third_party/sdl3/PROVENANCE.md). The build does not download it. Logging still uses the C++ standard library. The project requires C++20 and CMake 3.25+; the presets use Visual Studio 2022 on Windows. CI actions are pinned to immutable commit SHAs in the workflow.

A Linux configure succeeds without X11 or Wayland headers. The dummy and offscreen drivers are enough for tests and for `--window --dummy`. A desktop `--window` on Linux needs X11 or Wayland development headers at configure time. `libx11-dev` and `libxext-dev` are enough for the X11 driver this build enables. Optional X11 extras are left off so a partial install does not fail configure. Windows builds the Win32 driver. Vulkan is not linked.

Evaluate and pin rendering, physics, and editor dependencies in their own modules. Do not download unversioned dependency branches during builds.

## Verification

CTest verifies the sandbox, the CPU trace API, the GPU-free image harness, window and input, and the CLI. `input.mapping` checks edge state and the SDL scancode, mouse, and gamepad map without a display. `window.lifecycle` creates, resizes, pumps, and closes a dummy window, including a virtual gamepad. `foundation.smoke` also runs `lykoware_sandbox --window --dummy --frames 1`. None of these is a renderer test. `cli.commands` checks help text, `lyko run`, `lyko trace --frames`, and exit code 3 for `screenshot` and `run --scene`. `image.harness.api`, `image.harness.golden_pass`, and `image.harness.golden_fail` exercise the software pattern, committed PPM fixtures, and a comparison that is required to fail. Those tests are not renderer screenshots. Their outputs stay in the build directory. `perf.sandbox_startup` and `perf.cpu_fixture` enforce the provisional CPU-only thresholds in the root `CMakeLists.txt`. CLI integration covers real configure/build/test execution, literal Unicode/quoted arguments, child cwd, a Unicode trace path, failed configure/build/test commands, absent tools and zero-test failures. GitHub Actions repeats these on Windows Debug/Release, Linux Debug, ASan/UBSan, and clang-tidy 18 builds, then stages/runs the CLI. Current results are in STATUS.

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

See [CLI.md](CLI.md) for defaults, overrides, exit codes and the header contract. Installing the project stages both sandbox and lyko under bin/, which is where `lyko run` and `lyko trace` find the sandbox. `lyko trace --frames N` writes a CPU Chrome trace of the sandbox loop. `screenshot` and `run --scene` exit 3 with `unsupported: no renderer`.

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
