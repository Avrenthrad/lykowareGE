# Building lykowareGE

## Windows — primary target

Install:
- Visual Studio 2022 17.10 or newer, including **Desktop development with C++**, the MSVC v143 x64 toolset, and a Windows SDK.
- CMake 3.25 or newer, available on PATH.
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

With GCC 13+ (or another C++20 compiler supporting std::osyncstream), CMake 3.25+, and Ninja:

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
- tests/: executable smoke checks covering startup, version, help, and invalid arguments.
- assets/brand/: Lykoware logo vector set (SVG logo, icon, wordmark, mono variants), .ico, 512 px icon, preview sheet, BRAND.md guidelines and PROVENANCE.md.
- .github/workflows/build.yml: clean Windows Debug/Release builds, tests, staged execution, and downloadable artifacts; also Linux validation.

Build output is ignored by Git. CMakeUserPresets.json is available for local overrides.

## Dependencies and versions

Module 01 has **no third-party runtime dependencies**. Logging uses the C++ standard library. The project requires C++20 and CMake 3.25+; the presets use Visual Studio 2022 on Windows. CI actions are pinned to immutable commit SHAs in the workflow.

SDL3 is proposed for Module 02. Select a specific release and pin its source commit when integrating it. Evaluate and pin rendering, physics, and editor dependencies in their own modules. Do not download unversioned dependency branches during builds.

## Verification

CTest launches the actual sample executable and checks success output and error exit codes. GitHub Actions repeats these checks on clean checkouts, then stages and executes the Windows binary. Module 01's Windows verification checkbox stays open until its Windows workflow succeeds.
