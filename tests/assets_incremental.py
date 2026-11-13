"""07.02: observable diagnostics and unchanged-output preservation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(cooker, manifest, package, *extra, success=True):
    result = subprocess.run([sys.executable, str(cooker), str(manifest), str(package), *map(str, extra)],
                            capture_output=True, text=True, check=False)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


def main():
    cooker = Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "data.bin"
        source.write_bytes(b"one")
        manifest, package, report = (root / name for name in ["manifest.json", "package.json", "report.json"])
        graph = {"version": 1, "assets": [{"id": "example", "source": "data.bin"}]}
        manifest.write_text(json.dumps(graph), encoding="utf-8")
        invoke(cooker, manifest, package, "--report", report)
        assert json.loads(report.read_text())["status"] == "rebuilt"
        original = package.read_bytes()
        # Fixed old timestamp catches any rewrite without sleeps/filesystem timing assumptions.
        import os
        os.utime(package, ns=(1_000_000_000, 1_000_000_000))
        timestamp = package.stat().st_mtime_ns
        invoke(cooker, manifest, package, "--report", report)
        assert json.loads(report.read_text())["status"] == "unchanged"
        assert package.stat().st_mtime_ns == timestamp and package.read_bytes() == original
        source.write_bytes(b"two")
        invoke(cooker, manifest, package, "--report", report)
        assert json.loads(report.read_text())["status"] == "rebuilt" and package.read_bytes() != original
        good = package.read_bytes()
        source.unlink()
        result = invoke(cooker, manifest, package, success=False)
        diagnostic = json.loads(result.stdout)
        assert diagnostic["status"] == "failed" and "example" in diagnostic["diagnostic"] and "data.bin" in diagnostic["diagnostic"]
        assert "cannot read source" in result.stderr and package.read_bytes() == good
        for bad in [[], {"version": 1, "assets": ["bad"]}, {"version": 1, "assets": [{"id": "bad"}]}]:
            manifest.write_text(json.dumps(bad), encoding="utf-8")
            result = invoke(cooker, manifest, package, success=False)
            assert json.loads(result.stdout)["diagnostic"] and "Traceback" not in result.stderr
        source.write_bytes(b"two")
        manifest.write_text(json.dumps(graph), encoding="utf-8")
        for target in [source, manifest, package]:
            before = target.read_bytes()
            invoke(cooker, manifest, package, "--report", target, success=False)
            assert target.read_bytes() == before, "report cannot clobber input/output"
    print("incremental diagnostics passed")


if __name__ == "__main__":
    main()
