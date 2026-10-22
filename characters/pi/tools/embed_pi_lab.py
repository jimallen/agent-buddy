#!/usr/bin/env python3
"""Validate the committed Pi sprite export used to build its character pack."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CHARACTER = ROOT / "characters/pi"
TRACKS = (
    "right", "left", "up", "down", "up_right", "up_left", "down_right", "down_left",
    "surprise", "working", "complete", "attention", "attention_alternate",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(root=ROOT):
    character = root / "characters/pi"
    data = (character / "frames.bin").read_bytes()
    try:
        metadata = json.loads((character / "frames.json").read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(
            f"Pi pack metadata is missing or unreadable; run tools/export_pi_lab.py first: {error}") from error
    blocks = metadata.get("blocks")
    renders = character / "renders"
    if (metadata.get("formatVersion") != 1
            or metadata.get("encoding") != "zlib-rgb565-word-up-be"
            or metadata.get("width") != 412 or metadata.get("height") != 352
            or metadata.get("steps") != 24 or tuple(metadata.get("directions", [])) != TRACKS
            or metadata.get("frameBlocks") != 13 * 24 * 5
            or not isinstance(blocks, list) or len(blocks) != 13 * 24 * 5
            or metadata.get("dataBytes") != len(data)
            or metadata.get("dataSha256") != hashlib.sha256(data).hexdigest()):
        raise ValueError("Pi pack metadata is stale or invalid.")
    for index, block in enumerate(blocks):
        if (not isinstance(block, list) or len(block) != 2
                or any(type(value) is not int or value < 0 for value in block)
                or block[1] == 0 or block[0] + block[1] > len(data)):
            raise ValueError(f"Pi block {index} is invalid.")
    checks = {
        "modelSha256": character / "model/model.js",
        "exporterSha256": character / "model/export-sprites.mjs",
        "packExporterSha256": character / "tools/export_pi_lab.py",
        "packageLockSha256": character / "model/package-lock.json",
        "referenceSvgSha256": character / "reference/reference.svg",
        "manifestSha256": renders / "animation.json",
    }
    for key, path in checks.items():
        if metadata.get(key) != sha(path):
            raise ValueError(f"Pi pack is stale for {path}.")
    for name, digest in metadata.get("sourcePngSha256", {}).items():
        if sha(renders / name) != digest:
            raise ValueError(f"Pi render {name} changed after packing.")
    return True


if __name__ == "__main__":
    validate()
    print("Pi pack verified")
