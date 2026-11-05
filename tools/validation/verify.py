#!/usr/bin/env python3
"""Local native validation and honest source/test evidence (ADR 0046). No network operations."""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET

PROFILES = {
    "windows-debug": ("windows", "build/windows", "Debug"),
    "windows-release": ("windows", "build/windows", "Release"),
    "linux-debug": ("linux-debug", "build/linux-debug", "Debug"),
    "linux-sanitizers": ("linux-sanitizers", "build/linux-sanitizers", "Debug"),
    "linux-analysis": ("linux-analysis", "build/linux-analysis", "Debug"),
}


class ValidationError(RuntimeError):
    pass


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def write_report(path, report):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def git_bytes(git, source, *arguments):
    result = subprocess.run([git, "-C", str(source), *arguments], capture_output=True, check=False)
    if result.returncode != 0:
        raise ValidationError("Git could not identify repository inputs")
    return result.stdout


def source_snapshot(git, source):
    commit = git_bytes(git, source, "rev-parse", "HEAD").decode("ascii").strip()
    dirty = bool(git_bytes(git, source, "status", "--porcelain=v1", "-z", "--untracked-files=all"))
    names = git_bytes(git, source, "ls-files", "--cached", "--others", "--exclude-standard", "-z")
    digest = hashlib.sha256()
    for name in sorted(set(names.split(b"\0")) - {b""}):
        digest.update(len(name).to_bytes(8, "little"))
        digest.update(name)
        path = source / os.fsdecode(name)
        if path.is_symlink():
            data = os.fsencode(os.readlink(path))
            digest.update(b"symlink\0" + hashlib.sha256(data).digest())
        elif path.is_file():
            content = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    content.update(block)
            digest.update(b"file\0" + content.digest())
        elif not path.exists():
            digest.update(b"missing\0")
        else:
            raise ValidationError("Repository input is not a regular file or symlink")
    return {"commit": commit, "dirty": dirty, "fingerprint_sha256": digest.hexdigest()}


def execute(arguments, cwd, log, timeout):
    started = time.monotonic()
    result = {"command": list(map(str, arguments)), "returncode": None, "duration_seconds": 0.0}
    try:
        with log.open("wb") as output:
            completed = subprocess.run(result["command"], cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                                       timeout=timeout, check=False)
        result["returncode"] = completed.returncode
    except subprocess.TimeoutExpired:
        result["error"] = "command timed out"
    except OSError as error:
        result["error"] = str(error)
    finally:
        result["duration_seconds"] = round(time.monotonic() - started, 3)
    return result


def discover_tests(path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict):
        raise ValidationError("CTest discovery is not an object")
    tests = data.get("tests")
    if not isinstance(tests, list) or not tests:
        raise ValidationError("CTest discovered no tests")
    names = []
    for test in tests:
        if not isinstance(test, dict):
            raise ValidationError("CTest discovery contains an invalid test record")
        name = test.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ValidationError("CTest discovery contains missing/duplicate names")
        if any(p.get("name") == "DISABLED" and p.get("value") for p in test.get("properties", [])):
            raise ValidationError("CTest suite contains disabled tests")
        names.append(name)
    return names


def read_test_results(path, expected):
    root = ET.parse(path).getroot()
    # CTest emits a machine hostname even though validation does not need it.
    for node in root.iter():
        node.attrib.pop("hostname", None)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)
    results = {"passed": [], "skipped": [], "failed": []}
    seen = set()
    for case in root.iter("testcase"):
        name = case.get("name")
        if name not in expected or name in seen:
            raise ValidationError("JUnit contains unexpected/duplicate test names")
        seen.add(name)
        seconds = float(case.get("time", "0"))
        if not math.isfinite(seconds) or seconds < 0:
            raise ValidationError("JUnit contains an invalid test duration")
        item = {"name": name, "duration_seconds": seconds}
        skipped = case.find("skipped")
        if case.find("failure") is not None or case.find("error") is not None:
            results["failed"].append(item)
        elif skipped is not None:
            item["reason"] = skipped.get("message") or skipped.text or "CTest reported skipped"
            results["skipped"].append(item)
        elif case.get("status", "run") not in ("run", "passed"):
            raise ValidationError("JUnit contains a test that did not run")
        else:
            results["passed"].append(item)
    if seen != set(expected):
        raise ValidationError("JUnit does not contain every discovered test")
    return results


def read_cache(binary, profile, cmake_version=None):
    fields = {}
    for line in (binary / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"([^#/:][^:]*):[^=]+=(.*)", line)
        if match:
            fields[match[1]] = match[2]
    expected = {"BUILD_TESTING": "ON", "LYKOWARE_WARNINGS_AS_ERRORS": "ON",
                "LYKOWARE_SANITIZERS": "ON" if profile == "linux-sanitizers" else "OFF",
                "LYKOWARE_CLANG_TIDY": "ON" if profile == "linux-analysis" else "OFF"}
    if any(fields.get(name) != value for name, value in expected.items()):
        raise ValidationError("Configured test/warning/analysis flags do not match the requested profile")
    result = {key: fields[key] for key in (*expected, "CMAKE_GENERATOR", "CMAKE_BUILD_TYPE") if key in fields}
    cache_version = cmake_version or ".".join(fields.get("CMAKE_CACHE_" + part + "_VERSION", "") for part in ("MAJOR", "MINOR", "PATCH"))
    compiler_file = binary / "CMakeFiles" / cache_version / "CMakeCXXCompiler.cmake"
    if not compiler_file.is_file():
        raise ValidationError("Configured C++ compiler identity file is missing")
    text = compiler_file.read_text(encoding="utf-8", errors="replace")
    for variable in ("CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION"):
        match = re.search(r'set\(' + variable + r' "([^"]+)"\)', text)
        if not match:
            raise ValidationError("Configured C++ compiler identity is incomplete")
        result[variable] = match[1]
    return result


def validate_profile(options, name, directory, report_path, report):
    configure, binary_relative, configuration = PROFILES[name]
    directory.mkdir()
    profile = {"preset": name, "status": "running", "steps": [], "tests": None}
    report["profiles"].append(profile)
    write_report(report_path, report)

    def step(stage, command, allow_failure=False):
        print(f"[{name}] {stage}", flush=True)
        log = directory / (stage + ".log")
        result = execute(command, options.source, log, options.timeout_seconds)
        result.update({"stage": stage, "log": log.relative_to(report_path.parent).as_posix()})
        profile["steps"].append(result)
        write_report(report_path, report)
        if not allow_failure and result.get("returncode") != 0:
            raise ValidationError(f"{stage} failed; see {result['log']}")
        return result

    try:
        arguments = [options.cmake, "--preset", configure, "-DBUILD_TESTING=ON",
                     "-DLYKOWARE_WARNINGS_AS_ERRORS=ON", f"-DLYKOWARE_HOST_TOOL_JOBS={options.jobs}",
                     f"-DLYKOWARE_SANITIZERS={'ON' if name == 'linux-sanitizers' else 'OFF'}",
                     f"-DLYKOWARE_CLANG_TIDY={'ON' if name == 'linux-analysis' else 'OFF'}"]
        if name == "linux-analysis" and options.clang_tidy:
            arguments.append(f"-DLYKOWARE_CLANG_TIDY_EXE={options.clang_tidy}")
        step("configure", arguments)
        version = re.search(r"cmake version (\S+)", report["tools"]["cmake"])
        profile["configuration"] = read_cache(options.source / binary_relative, name, version.group(1) if version else None)
        step("build", [options.cmake, "--build", "--preset", name, "--parallel", str(options.jobs)])
        discovery = step("discover", [options.ctest, "--preset", name, "--show-only=json-v1"])
        names = discover_tests(report_path.parent / discovery["log"])
        profile["discovered_tests"] = names
        junit = directory / "tests.xml"
        tested = step("test", [options.ctest, "--preset", name, "--output-on-failure", "--no-tests=error",
                               "--output-junit", str(junit)], allow_failure=True)
        profile["tests"] = read_test_results(junit, names)
        if tested.get("returncode") != 0 or profile["tests"]["failed"]:
            raise ValidationError("CTest failed; individual results and logs are retained")
        if not profile["tests"]["passed"]:
            raise ValidationError("No test passed (an all-skipped suite is not validation)")
        prefix = directory / "stage"
        step("install", [options.cmake, "--install", str(options.source / binary_relative),
                         "--config", configuration, "--prefix", str(prefix)])
        executable = prefix / "bin" / ("lyko.exe" if os.name == "nt" else "lyko")
        version = step("staged-cli", [str(executable), "--version"])
        if not (report_path.parent / version["log"]).read_text(encoding="utf-8", errors="replace").startswith("lyko "):
            raise ValidationError("Installed CLI did not identify itself")
        profile["status"] = "passed_with_skips" if profile["tests"]["skipped"] else "passed"
    except KeyboardInterrupt:
        profile["status"] = "interrupted"
        profile["error"] = "validation interrupted"
        raise
    except (ValidationError, OSError, ValueError, ET.ParseError) as error:
        profile["status"] = "failed"
        profile["error"] = str(error)
    finally:
        write_report(report_path, report)


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def parse_arguments(arguments):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--preset", action="append", choices=tuple(PROFILES))
    parser.add_argument("--output", type=Path, help="fresh output directory (ignored build output or outside source)")
    parser.add_argument("--jobs", type=positive, default=2)
    parser.add_argument("--timeout-seconds", type=positive, default=7200)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--git", default="git")
    parser.add_argument("--clang-tidy", help="clang-tidy 18 executable for linux-analysis")
    options = parser.parse_args(arguments)
    if options.jobs > 64:
        parser.error("--jobs must be at most 64")
    options.preset = options.preset or (["windows-debug", "windows-release"] if platform.system() == "Windows" else ["linux-debug"])
    if len(set(options.preset)) != len(options.preset):
        parser.error("duplicate presets are not allowed")
    if options.source is None:
        options.source = next((parent for parent in Path(__file__).resolve().parents
                               if (parent / "CMakePresets.json").is_file()), None)
        if options.source is None:
            parser.error("--source is required outside a checkout")
    options.source = options.source.resolve()
    if not (options.source / "CMakePresets.json").is_file():
        parser.error("--source must contain CMakePresets.json")
    for name in ("cmake", "ctest", "git"):
        resolved = shutil.which(getattr(options, name))
        if not resolved:
            parser.error(f"{name} executable not found")
        setattr(options, name, resolved)
    try:
        root = Path(os.fsdecode(git_bytes(options.git, options.source, "rev-parse", "--show-toplevel")).strip()).resolve()
    except ValidationError as error:
        parser.error(str(error))
    if root != options.source:
        parser.error("--source must be the repository root")
    if "linux-analysis" in options.preset:
        options.clang_tidy = shutil.which(options.clang_tidy or "clang-tidy-18")
        if not options.clang_tidy:
            parser.error("clang-tidy 18 executable not found; use --clang-tidy PATH")
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    options.output = (options.output or options.source / "build/validation" / stamp).resolve()
    if options.output == options.source or options.output.is_relative_to(options.source):
        ignored = subprocess.run([options.git, "-C", str(options.source), "check-ignore", "-q", str(options.output)],
                                 capture_output=True, check=False)
        if ignored.returncode != 0:
            parser.error("output inside the repository must be ignored build output")
    if options.output.exists():
        parser.error("output directory already exists; choose a fresh directory")
    return options


def main(arguments=None):
    options = parse_arguments(arguments)
    options.output.mkdir(parents=True)
    report_path = options.output / "report.json"
    report = {"format": "lykoware.validation", "schema_version": 1, "status": "running",
              "validator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "started_utc": utc_now(), "finished_utc": None,
              "platform": {"system": platform.system(), "machine": platform.machine()},
              "source": None, "source_after": None, "source_unchanged": None,
              "tools": {}, "profiles": [], "errors": []}
    write_report(report_path, report)
    print(f"Evidence directory: {options.output}", flush=True)
    exit_code = 1
    try:
        report["source"] = source_snapshot(options.git, options.source)
        identity = report["source"]
        print(f"Source {identity['commit']} ({'dirty worktree' if identity['dirty'] else 'clean worktree'}), "
              f"fingerprint {identity['fingerprint_sha256']}", flush=True)
        tools = ["cmake", "ctest", "git"] + (["clang_tidy"] if "linux-analysis" in options.preset else [])
        for name in tools:
            log = options.output / (name + "-version.log")
            result = execute([getattr(options, name), "--version"], options.source, log, options.timeout_seconds)
            if result.get("returncode") != 0:
                raise ValidationError(f"{name} version check failed")
            version = log.read_text(encoding="utf-8", errors="replace").strip()
            if not version or (name == "clang_tidy" and not re.search(r"version\s+18\b", version)):
                raise ValidationError(f"{name} returned an unsupported version")
            report["tools"][name] = version.splitlines()[0] if name != "clang_tidy" else version
        for name in options.preset:
            validate_profile(options, name, options.output / name, report_path, report)
    except KeyboardInterrupt:
        report["errors"].append("validation interrupted")
        exit_code = 130
    except (ValidationError, OSError, ValueError) as error:
        report["errors"].append(str(error))
    finally:
        try:
            report["source_after"] = source_snapshot(options.git, options.source)
            report["source_unchanged"] = report["source_after"] == report["source"]
            if not report["source_unchanged"]:
                report["errors"].append("repository inputs changed during validation")
        except (ValidationError, OSError) as error:
            report["errors"].append(str(error))
        succeeded = (len(report["profiles"]) == len(options.preset) and not report["errors"]
                     and all(p["status"] in ("passed", "passed_with_skips") for p in report["profiles"]))
        if succeeded:
            report["status"] = ("passed_with_skips" if any(p["status"] == "passed_with_skips" for p in report["profiles"])
                                else "passed")
            exit_code = 0
        else:
            report["status"] = "interrupted" if exit_code == 130 else "failed"
        report["finished_utc"] = utc_now()
        write_report(report_path, report)
    print(f"Validation {report['status']}. Report: {report_path}", flush=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
