"""scene.capture_golden (ADR 0043): a real renderer capture through `lyko screenshot` matches its
committed golden within tolerance.

Usage: capture_golden.py LYKO IMAGE_COMPARE SCENES_DIR GOLDEN OUTPUT_DIR

The golden is the software Vulkan (lavapipe) rendering of the falling-crates scene after its
commands and 120 ticks, at 160x120. Hardware GPUs rasterise and round differently, so on them the
test reports Skipped rather than pass or fail: their goldens are future work.

Set LYKOWARE_UPDATE_GOLDEN=1 to replace the golden with this run's capture after a deliberate
rendering change; review the new image before committing it.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

TOLERANCE = 2        # per channel, raw bytes: shading may round differently between driver builds
MAX_MISMATCHED = 16  # pixels: a few edge pixels may flip; a whole crate (about 140 pixels here) may not

lyko, compare, scenes, golden, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
capture = output / "falling-crates.ppm"
report = output / "falling-crates.json"
result = subprocess.run([
    str(lyko), "screenshot", "--scene", str(scenes / "falling-crates.scene.json"),
    "--commands", str(scenes / "falling-crates.commands.json"), "--ticks", "120",
    "--output", str(capture), "--report", str(report), "--width", "160", "--height", "120",
    "--warmup", "1",
], capture_output=True, text=True, timeout=90, check=False)
if result.returncode == 77:
    print("SKIP: no Vulkan device for the capture:", result.stderr.strip())
    sys.exit(77)
assert result.returncode == 0, result.stdout + result.stderr
device = json.loads(report.read_text(encoding="utf-8"))["device"]
if device["type"] != "cpu":
    print(f"SKIP: the golden is the software Vulkan rendering; this device is {device['name']} ({device['type']})")
    sys.exit(77)

if os.environ.get("LYKOWARE_UPDATE_GOLDEN") == "1":
    shutil.copyfile(capture, golden)
    print(f"updated {golden} from {device['name']}; review it before committing")
    sys.exit(0)

compared = subprocess.run([str(compare), str(golden), str(capture), str(TOLERANCE), str(MAX_MISMATCHED),
                           str(output / "falling-crates-diff.ppm")], capture_output=True, text=True, check=False)
print(compared.stdout + compared.stderr, end="")
print(f"device: {device['name']}")
sys.exit(compared.returncode)
