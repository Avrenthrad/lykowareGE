"""Exercise the real CLI renderer/capture and its deterministic replay result."""
import json
from pathlib import Path
import subprocess
import sys

executable, source, destination = map(Path, sys.argv[1:])
destination.mkdir(parents=True, exist_ok=True)
reports = []
images = []
for repeat in range(2):
    image = destination / f"capture-{repeat}.ppm"
    report = destination / f"capture-{repeat}.json"
    result = subprocess.run([
        str(executable), "screenshot", "--scene", str(source / "falling-crates.scene.json"),
        "--commands", str(source / "falling-crates.commands.json"), "--ticks", "120",
        "--output", str(image), "--report", str(report), "--width", "160", "--height", "120",
        "--warmup", "2", "--measure-frames", "3",
    ], capture_output=True, text=True, timeout=90, check=False)
    if result.returncode == 77:
        print(result.stderr)
        sys.exit(77)
    assert result.returncode == 0, result.stdout + result.stderr
    images.append(image.read_bytes())
    reports.append(json.loads(report.read_text(encoding="utf-8")))
assert images[0] == images[1], "same scene/commands must capture identical pixels on the same driver"
assert images[0].startswith(b"P6\n160 120\n255\n")
assert len(set(images[0].split(b"\n", 3)[3])) > 10, "capture must contain rendered shading"
assert reports[0]["final_state_hash"] == reports[1]["final_state_hash"]
assert reports[0]["final_tick"] == 123
assert reports[0]["simulation_and_draw"]["samples"] == 3
assert reports[0]["device"]["name"]
assert reports[0]["peak_process_resident_bytes"] > 0
assert reports[0]["gpu_timestamp_ms"] is None
print("scene CLI capture/replay repeatability passed")
