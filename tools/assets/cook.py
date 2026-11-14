#!/usr/bin/env python3
"""GPT-owned source-free byte package cooker (ADR 0079). Standard library only."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def build(manifest_path):
    manifest_path = Path(manifest_path).resolve()
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    if document.get("version") != 1 or not isinstance(document.get("assets"), list):
        raise ValueError("manifest requires version 1 and an assets array")
    root = manifest_path.parent
    assets = {}
    for entry in document["assets"]:
        asset_id = entry.get("id")
        if not isinstance(asset_id, str) or not asset_id or len(asset_id.encode("utf-8")) > 256:
            raise ValueError("asset ID must be a nonempty string of at most 256 UTF-8 bytes")
        if asset_id in assets:
            raise ValueError(f"duplicate asset ID: {asset_id}")
        source = entry.get("source")
        if not isinstance(source, str) or not source:
            raise ValueError(f"{asset_id}: source must be a relative path")
        path = (root / source).resolve()
        if Path(source).is_absolute() or not path.is_relative_to(root):
            raise ValueError(f"{asset_id}: source escapes manifest directory")
        dependencies = entry.get("dependencies", [])
        if not isinstance(dependencies, list) or any(not isinstance(d, str) for d in dependencies):
            raise ValueError(f"{asset_id}: dependencies must be ID strings")
        if len(set(dependencies)) != len(dependencies):
            raise ValueError(f"{asset_id}: duplicate dependency")
        payload = path.read_bytes()
        assets[asset_id] = {"id": asset_id, "data": payload.hex(), "hash": fingerprint(payload),
                            "dependencies": dependencies}
    visiting, done = set(), set()

    def visit(asset_id):
        if asset_id not in assets:
            raise ValueError(f"missing dependency: {asset_id}")
        if asset_id in visiting:
            raise ValueError(f"dependency cycle at: {asset_id}")
        if asset_id in done:
            return
        visiting.add(asset_id)
        for dependency in assets[asset_id]["dependencies"]:
            visit(dependency)
        visiting.remove(asset_id)
        done.add(asset_id)

    for asset_id in assets:
        visit(asset_id)
    cooked = []
    for asset_id in sorted(assets):
        entry = assets[asset_id].copy()
        entry["dependencies"] = {dep: assets[dep]["hash"] for dep in sorted(entry["dependencies"])}
        cooked.append(entry)
    return (json.dumps({"format": "lykoware.package", "version": 1, "assets": cooked},
                       sort_keys=True, ensure_ascii=True, separators=(",", ":")) + "\n").encode("utf-8")


def write_atomic(path, data):
    path = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as file:
            temporary = Path(file.name)
            file.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        data = build(args.manifest)
        # The output must never overwrite a manifest or one of its source files.
        document = json.loads(args.manifest.read_text(encoding="utf-8"))
        protected = {args.manifest.resolve()}
        protected.update((args.manifest.parent / entry["source"]).resolve() for entry in document["assets"])
        if args.output.resolve() in protected:
            raise ValueError("output must not overwrite the manifest or an asset source")
        write_atomic(args.output, data)
        print(f"cooked {len(document['assets'])} assets")
        return 0
    except (OSError, ValueError, KeyError, TypeError, RecursionError) as error:
        print(f"cook failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
