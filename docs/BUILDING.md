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

The current program prints startup messages and exits. It has no window or 3D graphics yet.

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

## Structure

- engine/: reusable static C++20 library, linked as lykoware::engine.
- samples/sandbox/: separate executable containing sample-game code.
- tools/cli/: native lyko executable and private platform process runner.
- tests/: sandbox smoke tests and CLI integration tests with genuine CMake/CTest runs.
- .github/workflows/build.yml: clean Windows Debug/Release builds, tests, staged execution, and downloadable artifacts; also Linux validation.

Build output is ignored by Git. CMakeUserPresets.json is available for local overrides.

## Dependencies and versions

Module 01 has **no third-party runtime dependencies**. Logging uses the C++ standard library. The project requires C++20 and CMake 3.25+; the presets use Visual Studio 2022 on Windows. CI actions are pinned to immutable commit SHAs in the workflow.

SDL3 is proposed for Phase 1 window/input (formerly Module 02). Select a specific release and pin its source commit when integrating it. Evaluate and pin rendering, physics, and editor dependencies in their own modules. Do not download unversioned dependency branches during builds.

## Verification

CTest verifies the sandbox and CLI. CLI integration covers real configure/build/test execution, literal Unicode/quoted arguments, child cwd, failed configure/build/test commands, absent tools and zero-test failures. GitHub Actions repeats these on Windows Debug/Release, Linux Debug, ASan/UBSan, and clang-tidy 18 builds, then stages/runs the CLI. Current results are in STATUS.

## Native CLI

Bootstrap with CMake once, then use the executable:

```powershell
cmake --install build/windows --config Debug --prefix out/windows-debug
.\out\windows-debug\bin\lyko.exe build --source . --configure-preset windows --preset windows-debug
.\out\windows-debug\bin\lyko.exe test --source . --preset windows-debug
```

On Linux:

```sh
./build/linux-debug/tools/cli/lyko build --source . --preset linux-debug
./build/linux-debug/tools/cli/lyko test --source . --preset linux-debug
```

See [CLI.md](CLI.md) for defaults, overrides, exit codes and limitations. Installing the project stages both sandbox and lyko under bin/.
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

Python and analysis tools are development dependencies, not shipped game/runtime dependencies. No screenshot or device-performance gate is claimed before the relevant runtime and test hardware exist.
