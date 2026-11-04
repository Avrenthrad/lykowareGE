"""Exercise real process launches, argument boundaries, failures and CMake/CTest."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


lyko, fixture, cmake, ctest = (str(Path(arg).resolve()) for arg in sys.argv[1:])


def invoke(*args, expected=0, cwd=None):
    result = subprocess.run(
        [lyko, *map(str, args)], cwd=cwd, capture_output=True,
        encoding="utf-8", errors="replace", timeout=45, check=False,
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{args}: expected {expected}, got {result.returncode}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout + result.stderr


assert "Usage:" in invoke("--help")
assert "lyko trace --frames" in invoke("--help")
assert "run --scene FILE" in invoke("--help")
assert invoke("--version").startswith("lyko ")
assert "Unknown command" in invoke("nonsense", expected=2)
assert "requires --scene" in invoke("screenshot", expected=2)
assert "missing scene option value" in invoke("screenshot", "--harness", expected=2)
assert "requires --scene" in invoke("screenshot", "--output", "not-a-screenshot.ppm", expected=2)
assert "trace requires --frames" in invoke("trace", expected=2)
assert "integer from 1 to 1000000" in invoke("trace", "--frames", "0", expected=2)
assert "missing scene option value" in invoke("run", "--scene", expected=2)
assert "cannot read input" in invoke("run", "--scene", "room", expected=2)
assert "missing scene option value" in invoke("run", "--scene=room", expected=2)
assert "Foundation check complete" in invoke("run")
assert "run accepts no options" in invoke("run", "--extra", expected=2)

with tempfile.TemporaryDirectory(prefix="lyko tests ") as temporary:
    root = Path(temporary)
    scene_source = Path(__file__).resolve().parents[1] / "samples" / "scenes"
    report = root / "replay Ω report.json"
    scene_args = ("run", "--scene", scene_source / "falling-crates.scene.json",
                  "--commands", scene_source / "falling-crates.commands.json", "--ticks", "120")
    assert "ticks=120 entities=4" in invoke(*scene_args, "--report", report)
    first = json.loads(report.read_text(encoding="utf-8"))
    assert first["commands_applied"] == 3
    assert first["simulation"]["samples"] == 120
    invoke(*scene_args, "--report", report)
    second = json.loads(report.read_text(encoding="utf-8"))
    assert first["replay_state_hash"] == second["replay_state_hash"]
    assert first["gpu_timestamp_ms"] is None
    assert first["gpu_resident_bytes"] is None

    trace_dir = root / "trace Ω & ; literal"
    trace_dir.mkdir()
    trace_file = trace_dir / "cpu trace.json"
    traced = invoke("trace", "--frames", "3", "--output", trace_file)
    assert "Wrote CPU trace." in traced
    trace_text = trace_file.read_text(encoding="utf-8")
    assert trace_text.count('"name":"sandbox.frame"') == 3
    assert '"ph":"X"' in trace_text
    assert '"cat":"cpu"' in trace_text

    # This directory is passed to the fake tool so no generator limitations can
    # hide shell interpretation or Unicode/space argument bugs in lyko itself.
    source = root / "project Ω & ; literal"
    source.mkdir()
    (source / "CMakePresets.json").write_text("{}", encoding="utf-8")
    caller = root / "caller"
    caller.mkdir()
    log = source / "tool-calls.txt"

    def calls():
        text = log.read_text(encoding="utf-8")
        log.unlink()
        return text

    # Quotes, trailing backslashes and shell metacharacters must remain literal
    # argument contents, including on Windows CreateProcess/CRT boundaries.
    unusual = 'preset Ω spaces & ; $() "quoted" trail\\'
    invoke("build", "--source", source, "--configure-preset", "configure fixture",
           "--preset", unusual, "--cmake", fixture, cwd=caller)
    assert calls() == (
        "--preset\nconfigure fixture\nEND\n--build\n--preset\n" + unusual + "\nEND\n"
    )
    assert not (caller / "tool-calls.txt").exists()

    invoke("build", "--source", source, "--configure-preset", "fail-configure",
           "--cmake", fixture, expected=17)
    assert calls() == "--preset\nfail-configure\nEND\n"  # build was not started
    invoke("build", "--source", source, "--preset", "fail-build",
           "--cmake", fixture, expected=23)
    assert "--build\n--preset\nfail-build\n" in calls()

    invoke("test", "--source", source, "--preset", unusual, "--ctest", fixture)
    assert calls() == "--preset\n" + unusual + "\n--output-on-failure\n--no-tests=error\nEND\n"
    invoke("test", "--source", source, "--preset", "fail-configure",
           "--ctest", fixture, expected=17)
    calls()

    for args in (
        ("build", "--preset"), ("build", "--source", root / "absent"),
        ("build", "--source", caller), ("test", "--cmake", fixture),
        ("build", "--preset", "one", "--preset", "two"),
        ("build", "--preset", "-invalid"),
    ):
        invoke(*args, expected=2)
    invoke("build", "--source", source, "--cmake", root / "missing-tool", expected=127)
    assert not log.exists()

    # Finally exercise genuine configure/build/test presets in a fresh project,
    # not just a fixture that happens to accept the expected argument list.
    real = root / "real project Ω"
    real.mkdir()
    (real / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(cli_fixture NONE)\n'
        'enable_testing()\nadd_custom_target(fixture ALL COMMAND "${CMAKE_COMMAND}" -E echo built)\n'
        'add_test(NAME pass COMMAND "${CMAKE_COMMAND}" -E true)\n', encoding="utf-8",
    )
    presets = {
        "version": 6,
        "configurePresets": [{"name": "fixture", "generator": (
            "Visual Studio 17 2022" if os.name == "nt" else "Ninja"
        ), "binaryDir": "${sourceDir}/build"}],
        "buildPresets": [{"name": "fixture", "configurePreset": "fixture",
                          "configuration": "Debug"}],
        "testPresets": [{"name": "fixture", "configurePreset": "fixture",
                         "configuration": "Debug"}],
    }
    (real / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
    invoke("build", "--source", real, "--configure-preset", "fixture",
           "--preset", "fixture", "--cmake", cmake, cwd=caller)
    assert "100% tests passed" in invoke(
        "test", "--source", real, "--preset", "fixture", "--ctest", ctest, cwd=caller,
    )
    # A valid CTest invocation that finds zero tests must not report success.
    (real / "build" / "CTestTestfile.cmake").write_text("", encoding="utf-8")
    invoke("test", "--source", real, "--preset", "fixture", "--ctest", ctest,
           expected=8)

print("CLI integration passed: real tools, Unicode/literal arguments, cwd and failure propagation.")
