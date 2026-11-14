"""Cook/load integration; runs without graphics or source-tree runtime access."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(*args, success=True):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, check=False)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


def main():
    cooker, probe = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        sources = root / "sources"
        sources.mkdir()
        (sources / "model.bin").write_bytes(bytes([1, 2, 3]))
        (sources / "texture.bin").write_bytes(bytes([0, 255, 17]))
        manifest = sources / "manifest.json"
        document = {"version": 1, "assets": [
            {"id": "texture", "source": "texture.bin"},
            {"id": "model", "source": "model.bin", "dependencies": ["texture"]}]}
        manifest.write_text(json.dumps(document), encoding="utf-8")
        package = root / "package.json"
        run(sys.executable, cooker, manifest, package)
        original = package.read_bytes()
        run(sys.executable, cooker, manifest, package)
        assert package.read_bytes() == original, "unchanged cook is byte-stable"
        reordered = copy.deepcopy(document)
        reordered["assets"].reverse()
        manifest.write_text(json.dumps(reordered), encoding="utf-8")
        run(sys.executable, cooker, manifest, package)
        assert package.read_bytes() == original, "manifest order does not affect package"
        run(probe, package)
        run(sys.executable, cooker, manifest, sources / "model.bin", success=False)
        assert (sources / "model.bin").read_bytes() == bytes([1, 2, 3]), "source never overwritten"
        for bad in [
            {"version": 2, "assets": []},
            {"version": 1, "assets": [{"id": "x", "source": "absent"}]},
            {"version": 1, "assets": [{"id": "x", "source": "../package.json"}]},
            {"version": 1, "assets": [{"id": "x", "source": "model.bin", "dependencies": ["missing"]}]},
            {"version": 1, "assets": [{"id": "x", "source": "model.bin", "dependencies": ["x"]}]},
            {"version": 1, "assets": [document["assets"][0], document["assets"][0]]}]:
            manifest.write_text(json.dumps(bad), encoding="utf-8")
            run(sys.executable, cooker, manifest, package, success=False)
            assert package.read_bytes() == original, "failed cook preserves last good output"
        cooked = json.loads(original)
        variants = []
        bad = copy.deepcopy(cooked)
        bad["assets"][1]["data"] = "00"  # deliberate stale payload
        variants.append(bad)
        bad = copy.deepcopy(cooked)
        bad["assets"][0]["dependencies"]["texture"] = "0000000000000000"
        variants.append(bad)
        bad = copy.deepcopy(cooked)
        bad["assets"].pop()
        variants.append(bad)
        bad = copy.deepcopy(cooked)
        bad["assets"][1]["dependencies"]["model"] = bad["assets"][0]["hash"]
        variants.append(bad)
        bad = copy.deepcopy(cooked)
        bad["version"] = 99
        variants.append(bad)
        bad = copy.deepcopy(cooked)
        bad["assets"].append(bad["assets"][0])
        variants.append(bad)
        for bad in variants:
            package.write_text(json.dumps(bad), encoding="utf-8")
            run(probe, package, "--reject")
        # New dependency payload/hash spliced in without recooking its dependent.
        manifest.write_text(json.dumps(document), encoding="utf-8")
        (sources / "texture.bin").write_bytes(b"changed")
        fresh = root / "fresh.json"
        run(sys.executable, cooker, manifest, fresh)
        stale = copy.deepcopy(cooked)
        stale["assets"][1] = json.loads(fresh.read_bytes())["assets"][1]
        package.write_text(json.dumps(stale), encoding="utf-8")
        run(probe, package, "--reject")
        package.write_bytes(original)
        shutil.rmtree(sources)
        run(probe, package)  # runtime only has package bytes
        package.write_bytes(original[:len(original) // 2])
        run(probe, package, "--reject")
    print("asset cook/load passed")


if __name__ == "__main__":
    main()
