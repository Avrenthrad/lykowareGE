# lyko — native build/test tools

Implemented Phase 0 scope: C++20 executable, installed in bin/. Bootstrap it using CMake; it cannot build itself before it exists.

## Commands

- lyko build [--source DIR] [--configure-preset NAME] [--preset NAME] [--cmake EXECUTABLE]
- lyko test [--source DIR] [--preset NAME] [--ctest EXECUTABLE]
- lyko run
- lyko trace --frames N [--output FILE]
- lyko --help
- lyko --version

`run` launches the console sample `lykoware_sandbox` and returns its exit code. It loads no scene. `trace --frames N` launches that sandbox's CPU loop for N iterations and writes a Chrome trace JSON file. The capture is CPU-only: there is no renderer and no GPU event. N is an integer from 1 to 1000000. The default file is `lykoware-cpu-trace.json` in the current directory; `--output FILE` chooses another path. `run --scene` and `screenshot` are not implemented: each prints `unsupported: no renderer` and returns exit code 3. Extra `screenshot` arguments do not change that result. Help text names those two only as unavailable and does not offer a PPM or harness command. Do not treat that message as a successful capture.

The engine has a separate GPU-free image harness ([ADR 0007](adr/0007-image-harness.md)). CTest runs it through `lykoware_image_harness_test`. That path is not `lyko screenshot` and not a renderer screenshot.

Source defaults to the caller's current directory and must contain CMakePresets.json. Windows defaults to configure preset windows and build/test preset windows-debug; Linux defaults to linux-debug. For Release explicitly choose windows-release. build runs configure first, stops on failure, then invokes the build preset. test does not build; it invokes CTest with output-on-failure and treats zero discovered tests as failure.

Configure and build presets can differ. When overriding the build preset, supply a matching configure preset if needed; lyko deliberately does not parse CMake preset inheritance itself.

CMake and CTest are found on PATH by default. Explicit --cmake/--ctest paths may include spaces/Unicode; relative executable paths containing a parent path are resolved relative to the caller before launching. Only pass a tool you intend to execute. Tool output is inherited directly; it is not a JSON protocol.

## Exit codes

- 0: successful command/help/version.
- 2: invalid command/options/source or CLI internal error.
- 3: renderer capability unavailable (`screenshot`, `run --scene`). The stderr line contains `unsupported: no renderer`. The sandbox is not started. `trace` is not this case.
- 127: executable could not launch.
- Other child exit codes: passed through. POSIX signals map to 128 + signal; Windows exit codes outside signed int range map to 1.

A child can itself return 2/3/127; messages identify whether the failure came from the CLI or tool.

## Process behavior and limits

Processes launch directly through CreateProcessW or fork/execvp, never through a shell. Arguments remain literal; Unicode Windows arguments use wide entry points and CRT-compatible quoting. The child works in the selected project directory without changing the parent directory, and inherits streams/environment. The CLI waits for completion.

Every subcommand goes through `dispatch` in [tools/cli/commands.hpp](../tools/cli/commands.hpp). That is one fixed handler table inside the tool. It is not a serializable editor command bus, and scripting does not have a second copy of these operations. Build and test behavior is unchanged.

`run` looks for `lykoware_sandbox` beside the `lyko` executable (the install/`bin` layout) and otherwise uses the sandbox path recorded when this `lyko` was built. It passes no arguments. `trace` uses that same lookup, then passes `--frames` and `--trace` and waits. The sandbox writes the Chrome trace. There is no renderer, scene loader, screenshot, or GPU capture.

This is a developer tool, not the future editor command bus, job scheduler, or remote execution service. Cancellation, process-tree management, and machine-readable event output are not implemented. Build-only commands do not require game data or schema decisions.
When rebuilding lyko itself on Windows, run an installed copy outside its build output directory so the linker can replace the build executable. Restage after that command exits.

## Header contract

[tools/cli/commands.hpp](../tools/cli/commands.hpp) and [tools/cli/process.hpp](../tools/cli/process.hpp) state the contracts next to the declarations. They are tool headers, not installed engine APIs.

- Ownership: argument strings and paths belong to the caller. `dispatch` and `run_process` do not keep them after return. Child handles are local and closed before return. Standard streams are inherited, not captured.
- Lifetime: both calls block until the handler or child finishes. No queued command, callback, or process handle remains. The caller may free the arguments immediately after return.
- Threading: one CLI thread. Neither function is synchronized. Do not call them concurrently. No handler starts a background thread.
- Failure: bad usage returns 2 and prints `lyko:` plus the reason. That includes `trace` without a valid `--frames` value. `screenshot` and `run --scene` return 3 and print `unsupported: no renderer` without launching a renderer. A missing or unstartable executable returns 127. `trace --frames N` returns the sandbox status, including 1 when the trace file cannot be written. Other statuses come from the child. Wait failures throw `std::runtime_error`; the executable boundary reports an unexpected exception as exit 2. The CPU trace API's own ownership, lifetime, threading, and failure contract is in [engine/include/lykoware/trace.hpp](../engine/include/lykoware/trace.hpp). The image harness contract is in [engine/include/lykoware/image.hpp](../engine/include/lykoware/image.hpp). It is not a lyko command.
