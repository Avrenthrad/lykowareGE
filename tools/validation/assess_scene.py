"""Assess local offscreen evidence; never certifies Phase 1 or presentation pacing."""
import argparse
import json
import math
from pathlib import Path
import statistics
import re


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def positive(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{name} must be finite and positive")
    return value


def assess(directory, budget=None):
    result = {"format": "lykoware.offscreen-assessment", "schema_version": 1,
              "status": "failed", "errors": [], "runs": [], "budget_passed": None,
              "phase1_accepted": False, "recommended_profile_validated": False,
              "scope": "Synchronous offscreen fixture only; no presentation pacing or GPU residency measurement."}
    try:
        hardware = load(directory / "hardware.json")
        if not isinstance(hardware, dict):
            raise ValueError("hardware must be an object")
        if hardware["format"] != "lykoware.hardware-evidence" or type(hardware["schema_version"]) is not int or hardware["schema_version"] != 2:
            raise ValueError("hardware schema must be version 2")
        config = hardware["fixture"]
        if not isinstance(config, dict):
            raise ValueError("fixture must be an object")
        if not isinstance(hardware["source_dirty"], bool):
            raise ValueError("source_dirty must be boolean")
        for field, size in (("commit", 40), ("executable_sha256", 64)):
            if not isinstance(hardware[field], str) or not re.fullmatch(f"[0-9a-fA-F]{{{size}}}", hardware[field]):
                raise ValueError(f"invalid {field}")
        for field in ("scene_sha256", "commands_sha256"):
            if not isinstance(config[field], str) or not re.fullmatch("[0-9a-fA-F]{64}", config[field]):
                raise ValueError(f"invalid {field}")
        for field in ("frames", "width", "height", "ticks", "warmup"):
            value = positive(config[field], field)
            if not isinstance(value, int):
                raise ValueError(f"{field} must be an integer")
        result["source"] = {"commit": hardware["commit"], "dirty": hardware["source_dirty"],
                            "executable_sha256": hardware["executable_sha256"]}
        inventory = hardware.get("nvidia_gpus", [])
        if not isinstance(inventory, list):
            raise ValueError("nvidia_gpus must be a list")
        for gpu in inventory:
            if not isinstance(gpu, dict):
                raise ValueError("GPU inventory entries must be objects")
            if not isinstance(gpu["name"], str) or not gpu["name"].strip() or not isinstance(gpu["driver_version"], str) or not gpu["driver_version"].strip():
                raise ValueError("GPU inventory name/driver missing")
            positive(gpu["total_memory_mib"], "total_memory_mib")
        result["gpu_inventory"] = inventory
        result["inventory_matches_selected_device"] = False
        identity = None
        for index in range(1, 4):
            scene = load(directory / f"scene-{index}.json")
            if not isinstance(scene, dict):
                raise ValueError(f"run {index}: scene must be an object")
            if scene["format"] != "lykoware.measurement" or type(scene["schema_version"]) is not int or scene["schema_version"] != 1:
                raise ValueError(f"run {index}: unsupported scene schema")
            if scene.get("hardware_gpu") is not True:
                raise ValueError(f"run {index}: physical GPU required")
            for field, key in (("width", "width"), ("height", "height"), ("scene_ticks", "ticks"), ("warmup_draws", "warmup")):
                if scene[field] != config[key]:
                    raise ValueError(f"run {index}: {field} mismatch")
            samples = scene["frame_times_ms"]
            if not isinstance(samples, list) or len(samples) != config["frames"]:
                raise ValueError(f"run {index}: sample count mismatch")
            for sample in samples:
                if isinstance(sample, bool) or not isinstance(sample, (int, float)) or not math.isfinite(sample) or sample < 0:
                    raise ValueError(f"run {index}: invalid duration")
            memory = positive(scene["peak_process_resident_bytes"], "peak_process_resident_bytes")
            device = scene["device"]
            if not isinstance(device, dict) or not isinstance(device.get("name"), str) or not device["name"].strip():
                raise ValueError(f"run {index}: device identity missing")
            if device.get("type") not in ("discrete", "integrated"):
                raise ValueError(f"run {index}: physical GPU type required")
            for field in ("vendor_id", "device_id", "driver_version"):
                if type(device[field]) is not int or device[field] < 0:
                    raise ValueError(f"run {index}: invalid device {field}")
            for field in ("replay_state_hash", "final_state_hash", "final_tick"):
                if type(scene[field]) is not int or scene[field] < 0:
                    raise ValueError(f"run {index}: invalid {field}")
            # Pin both replay and measured final state; scene samples advance simulation.
            current = tuple(scene[k] for k in ("replay_state_hash", "final_state_hash", "final_tick")) + tuple(device[k] for k in ("name", "vendor_id", "device_id", "driver_version"))
            if scene["final_tick"] != config["ticks"] + config["frames"]:
                raise ValueError(f"run {index}: final tick mismatch")
            if identity is not None and current != identity:
                raise ValueError(f"run {index}: state/device mismatch")
            identity = current
            ordered = sorted(samples)
            result["runs"].append({"run": index, "samples": len(samples), "mean_ms": statistics.fmean(samples),
                                   "p99_ms": ordered[math.ceil(len(samples) * .99) - 1], "max_ms": ordered[-1],
                                   "peak_process_resident_bytes": memory})
        result["selected_device"] = device
        matches = [gpu for gpu in inventory if gpu["name"].casefold().strip() == device["name"].casefold().strip()]
        result["inventory_matches_selected_device"] = len(matches) == 1
        result["status"] = "collected" if len(matches) == 1 else "incomplete"
        result["remaining"] = ["Target-PC desktop/presentation checks", "GPU residency and representative workloads", "Calibrated hardware budgets and Phase 1 review"]
        if len(matches) != 1:
            result["remaining"].append("Unambiguous selected-GPU VRAM/driver inventory")
        if budget is not None:
            if not isinstance(budget, dict) or budget.get("format") != "lykoware.offscreen-budget" or type(budget.get("schema_version")) is not int or budget.get("schema_version") != 1 or budget.get("status") != "provisional":
                raise ValueError("budget must be version 1 and explicitly provisional")
            if budget.get("fixture") != config:
                raise ValueError("budget fixture must match hashes and run configuration")
            limits = budget["limits"]
            allowed = {"mean_ms", "p99_ms", "max_ms", "peak_process_resident_bytes"}
            if not isinstance(limits, dict) or not limits or set(limits) - allowed:
                raise ValueError("budget needs recognized nonempty limits")
            for key, value in limits.items():
                positive(value, key)
            result["budget"] = budget
            result["budget_passed"] = True
            for run in result["runs"]:
                for key, limit in limits.items():
                    if run[key] > limit:
                        result["errors"].append(f"run {run['run']}: {key} exceeds {limit}")
                        result["budget_passed"] = False
            if not result["budget_passed"]:
                result["status"] = "failed"
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as error:
        result["errors"].append(str(error))
        result["status"] = "failed"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--budget", type=Path)
    args = parser.parse_args()
    try:
        budget = load(args.budget) if args.budget else None
        if args.budget and not isinstance(budget, dict):
            raise ValueError("budget must be an object")
        result = assess(args.directory, budget)
    except (OSError, ValueError) as error:
        result = {"format": "lykoware.offscreen-assessment", "schema_version": 1,
                  "status": "failed", "errors": [str(error)], "budget_passed": None,
                  "phase1_accepted": False, "recommended_profile_validated": False}
    try:
        (args.directory / "assessment.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(1, f"assessment failed: {error}\n")
    print(f"Offscreen evidence: {result['status']}; Phase 1 acceptance remains open.")
    return 0 if result["status"] == "collected" else 1


if __name__ == "__main__":
    raise SystemExit(main())
