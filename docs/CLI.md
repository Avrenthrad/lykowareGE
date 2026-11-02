# lyko — native build/test tools

Implemented Phase 0 scope: C++20 executable, installed in bin/. Bootstrap it using CMake; it cannot build itself before it exists.

## Commands

- lyko build [--source DIR] [--configure-preset NAME] [--preset NAME] [--cmake EXECUTABLE]
- lyko test [--source DIR] [--preset NAME] [--ctest EXECUTABLE]
- lyko --help
- lyko --version

Source defaults to the caller's current directory and must contain CMakePresets.json. Windows defaults to configure preset windows and build/test preset windows-debug; Linux defaults to linux-debug. For Release explicitly choose windows-release. build runs configure first, stops on failure, then invokes the build preset. test does not build; it invokes CTest with output-on-failure and treats zero discovered tests as failure.

Configure and build presets can differ. When overriding the build preset, supply a matching configure preset if needed; lyko deliberately does not parse CMake preset inheritance itself.

CMake and CTest are found on PATH by default. Explicit --cmake/--ctest paths may include spaces/Unicode; relative executable paths containing a parent path are resolved relative to the caller before launching. Only pass a tool you intend to execute. Tool output is inherited directly; it is not a JSON protocol.

## Exit codes

- 0: successful command/help/version.
- 2: invalid command/options/source or CLI internal error.
- 3: recognized future command (run, screenshot, trace) unavailable.
- 127: executable could not launch.
- Other child exit codes: passed through. POSIX signals map to 128 + signal; Windows exit codes outside signed int range map to 1.

A child can itself return 2/3/127; messages identify whether the failure came from the CLI or tool.

## Process behavior and limits

Processes launch directly through CreateProcessW or fork/execvp, never through a shell. Arguments remain literal; Unicode Windows arguments use wide entry points and CRT-compatible quoting. The child works in the selected project directory without changing the parent directory, and inherits streams/environment. The CLI waits for completion.

This is a single-command developer tool, not the future editor command bus, job scheduler, sandbox or remote execution service. Cancellation/process-tree management and machine-readable event output are not yet implemented. Build-only commands do not require game data/schema decisions.
When rebuilding lyko itself on Windows, run an installed copy outside its build output directory so the linker can replace the build executable. Restage after that command exits.

Scene execution and screenshot/trace capture are deferred to the runtime/renderer; no empty successful implementation is exposed.
