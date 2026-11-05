"""Failure-oriented tests for local build/test evidence, without compiling a fixture engine."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

CMAKE, CTEST = sys.argv[1:3] if len(sys.argv) == 3 else (shutil.which("cmake"), shutil.which("ctest"))
SCRIPT = Path(__file__).resolve().parents[1] / "tools/validation/verify.py"
spec = importlib.util.spec_from_file_location("verify", SCRIPT)
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class ValidationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="validation Ω ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.source = self.work / "source with spaces Ω"
        self.source.mkdir()
        (self.source / "CMakePresets.json").write_text('{"version":6}', encoding="utf-8")
        (self.source / ".gitignore").write_text("build/\n", encoding="utf-8")
        (self.source / "note.txt").write_text("original\n", encoding="utf-8")
        self.git = shutil.which("git")
        self.git_run("init", "-q")
        self.git_run("add", ".")
        self.git_run("-c", "user.name=ValidationFixture", "-c", "user.email=fixture@example.invalid",
                     "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
        self.calls = []
        self.mode = "pass"

    def git_run(self, *arguments):
        subprocess.run([self.git, "-C", str(self.source), *arguments], check=True, capture_output=True)

    def fake_execute(self, arguments, cwd, log, timeout):
        self.calls.append(list(map(str, arguments)))
        stage = log.stem
        log.write_text("tool completed\n", encoding="utf-8")
        result = {"command": list(map(str, arguments)), "returncode": 0, "duration_seconds": 0.01}
        if stage.endswith("-version"):
            log.write_text("tool version 18.1.8\n", encoding="utf-8")
        elif stage == "configure":
            configure = arguments[arguments.index("--preset") + 1]
            binary = self.source / ("build/windows" if configure == "windows" else "build/linux-debug")
            binary.mkdir(parents=True, exist_ok=True)
            (binary / "CMakeCache.txt").write_text(
                "BUILD_TESTING:BOOL=ON\nLYKOWARE_WARNINGS_AS_ERRORS:BOOL=ON\n"
                "LYKOWARE_SANITIZERS:BOOL=OFF\nLYKOWARE_CLANG_TIDY:BOOL=OFF\n"
                "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_CACHE_MAJOR_VERSION:INTERNAL=3\n"
                "CMAKE_CACHE_MINOR_VERSION:INTERNAL=31\nCMAKE_CACHE_PATCH_VERSION:INTERNAL=6\n", encoding="utf-8")
            compiler = binary / "CMakeFiles/3.31.6"
            compiler.mkdir(parents=True, exist_ok=True)
            (compiler / "CMakeCXXCompiler.cmake").write_text(
                'set(CMAKE_CXX_COMPILER_ID "GNU")\nset(CMAKE_CXX_COMPILER_VERSION "fixture")\n', encoding="utf-8")
            if self.mode == "bad_cache":
                (binary / "CMakeCache.txt").write_text("BUILD_TESTING:BOOL=OFF\n", encoding="utf-8")
        elif stage == "discover":
            tests = [{"name": name, "properties": []} for name in ("cpu.check", "gpu.capture")]
            if self.mode == "zero":
                tests = []
            if self.mode == "disabled":
                tests[0]["properties"] = [{"name": "DISABLED", "value": True}]
            data = [] if self.mode == "bad_discovery" else {"tests": tests}
            log.write_text("{broken" if self.mode == "malformed_json" else json.dumps(data), encoding="utf-8")
        elif stage == "test":
            if self.mode == "interrupted":
                raise KeyboardInterrupt()
            if self.mode == "source_changed":
                (self.source / "note.txt").write_text("changed during test\n", encoding="utf-8")
            output = Path(arguments[arguments.index("--output-junit") + 1])
            root = ET.Element("testsuite", hostname="private-machine")
            for name in ("cpu.check", "gpu.capture"):
                if self.mode == "missing_case" and name == "gpu.capture":
                    continue
                case = ET.SubElement(root, "testcase", name=name, time="0.01", status="run")
                if self.mode == "test_failure" and name == "cpu.check":
                    ET.SubElement(case, "failure", message="failed")
                    result["returncode"] = 8
                if self.mode == "all_skipped" or (self.mode == "gpu_skip" and name == "gpu.capture"):
                    case.set("status", "notrun")
                    ET.SubElement(case, "skipped", message="GPU unavailable")
            if self.mode == "duplicate_case":
                ET.SubElement(root, "testcase", name="cpu.check", time="0", status="run")
            if self.mode == "malformed_xml":
                output.write_text("<broken", encoding="utf-8")
            elif self.mode != "missing_xml":
                ET.ElementTree(root).write(output, encoding="utf-8", xml_declaration=True)
        elif stage == "install":
            prefix = Path(arguments[arguments.index("--prefix") + 1])
            binary = prefix / "bin"
            binary.mkdir(parents=True)
            (binary / ("lyko.exe" if os.name == "nt" else "lyko")).write_bytes(b"fixture")
        elif stage == "staged-cli":
            log.write_text("lyko fixture\n", encoding="utf-8")
        if self.mode == "fail_" + stage:
            result["returncode"] = 1
        if self.mode == "debug_failure" and stage == "build" and log.parent.name == "windows-debug":
            result["returncode"] = 1
        if self.mode == "timeout" and stage == "build":
            result.update(returncode=None, error="command timed out")
        return result

    def invoke(self, mode="pass", extra=()):
        self.mode = mode
        output = self.work / ("evidence " + mode)
        with patch.object(verify, "execute", side_effect=self.fake_execute), contextlib.redirect_stdout(io.StringIO()):
            code = verify.main(["--source", str(self.source), "--preset", "linux-debug", "--output", str(output),
                                "--cmake", str(CMAKE), "--ctest", str(CTEST), *extra])
        return code, json.loads((output / "report.json").read_text(encoding="utf-8")), output

    def test_complete_evidence_and_private_metadata(self):
        code, report, output = self.invoke()
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "passed")
        self.assertTrue(report["source_unchanged"])
        self.assertFalse(report["source"]["dirty"])
        self.assertEqual(len(report["source"]["fingerprint_sha256"]), 64)
        profile = report["profiles"][0]
        self.assertEqual(len(profile["tests"]["passed"]), 2)
        self.assertEqual(profile["configuration"]["CMAKE_CXX_COMPILER_ID"], "GNU")
        self.assertIn("--no-tests=error", self.calls[-3])
        self.assertNotIn("hostname", (output / "linux-debug/tests.xml").read_text())
        self.assertFalse((output / "report.tmp").exists())

    def test_windows_default_runs_both_configurations(self):
        output = self.work / "windows evidence"
        with patch.object(verify, "execute", side_effect=self.fake_execute), \
                patch.object(verify.platform, "system", return_value="Windows"), \
                contextlib.redirect_stdout(io.StringIO()):
            code = verify.main(["--source", str(self.source), "--output", str(output),
                                "--cmake", str(CMAKE), "--ctest", str(CTEST)])
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(code, 0)
        self.assertEqual([p["preset"] for p in report["profiles"]], ["windows-debug", "windows-release"])
        configurations = [call[call.index("--config") + 1] for call in self.calls if "--install" in call]
        self.assertEqual(configurations, ["Debug", "Release"])

    def test_windows_failure_does_not_hide_other_configuration(self):
        self.mode = "debug_failure"
        output = self.work / "partial windows evidence"
        with patch.object(verify, "execute", side_effect=self.fake_execute), \
                patch.object(verify.platform, "system", return_value="Windows"), \
                contextlib.redirect_stdout(io.StringIO()):
            code = verify.main(["--source", str(self.source), "--output", str(output),
                                "--cmake", str(CMAKE), "--ctest", str(CTEST)])
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(code, 1)
        self.assertEqual([p["status"] for p in report["profiles"]], ["failed", "passed"])
        self.assertEqual(report["status"], "failed")

    def test_gpu_skip_is_explicit(self):
        code, report, _ = self.invoke("gpu_skip")
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "passed_with_skips")
        self.assertEqual(report["profiles"][0]["tests"]["skipped"][0]["name"], "gpu.capture")

    def test_misleading_success_is_refused(self):
        for mode in ("zero", "disabled", "all_skipped", "missing_case", "duplicate_case", "malformed_xml", "bad_discovery", "malformed_json",
                     "missing_xml", "bad_cache", "test_failure", "fail_configure", "fail_build", "fail_install",
                     "fail_staged-cli", "timeout"):
            with self.subTest(mode=mode):
                code, report, _ = self.invoke(mode)
                self.assertEqual(code, 1)
                self.assertEqual(report["status"], "failed")
                self.assertEqual(report["profiles"][0]["status"], "failed")

    def test_source_changes_invalidate_otherwise_successful_run(self):
        code, report, _ = self.invoke("source_changed")
        self.assertEqual(code, 1)
        self.assertFalse(report["source_unchanged"])
        self.assertNotEqual(report["source"]["fingerprint_sha256"], report["source_after"]["fingerprint_sha256"])

    def test_interruption_is_not_a_pass(self):
        code, report, _ = self.invoke("interrupted")
        self.assertEqual(code, 130)
        self.assertEqual(report["status"], "interrupted")
        self.assertEqual(report["profiles"][0]["status"], "interrupted")

    def test_stale_and_unignored_output_are_refused(self):
        existing = self.work / "existing"
        existing.mkdir()
        for destination in (existing, self.source / "unignored-report"):
            with self.subTest(destination=destination), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    verify.parse_arguments(["--source", str(self.source), "--output", str(destination),
                                            "--cmake", str(CMAKE), "--ctest", str(CTEST)])
                self.assertEqual(error.exception.code, 2)
        self.assertFalse((self.source / "unignored-report").exists())

    def test_fingerprint_includes_untracked_inputs(self):
        before = verify.source_snapshot(self.git, self.source)
        (self.source / "new-source.txt").write_text("new input", encoding="utf-8")
        after = verify.source_snapshot(self.git, self.source)
        self.assertTrue(after["dirty"])
        self.assertNotEqual(before["fingerprint_sha256"], after["fingerprint_sha256"])

    def test_actual_ctest_skip_format(self):
        fixture = self.work / "real CTest"
        fixture.mkdir()
        python = Path(sys.executable).as_posix()
        (fixture / "CTestTestfile.cmake").write_text(
            f'add_test(cpu.check "{python}" "-c" "print(123)")\n'
            f'add_test(gpu.capture "{python}" "-c" "raise SystemExit(77)")\n'
            'set_tests_properties(gpu.capture PROPERTIES SKIP_RETURN_CODE 77)\n', encoding="utf-8")
        xml = fixture / "tests.xml"
        run = subprocess.run([str(CTEST), "--test-dir", str(fixture), "--output-junit", str(xml)],
                             capture_output=True, check=False)
        self.assertEqual(run.returncode, 0, run.stdout)
        results = verify.read_test_results(xml, ["cpu.check", "gpu.capture"])
        self.assertEqual(len(results["passed"]), 1)
        self.assertEqual(len(results["skipped"]), 1)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
