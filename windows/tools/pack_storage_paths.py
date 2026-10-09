#!/usr/bin/env python3
"""Compact existing detached Storage layers; never compile or query a filesystem."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def named(node, name):
    if node.get("name") == name:
        return node
    for child in node.get("children", []):
        result = named(child, name)
        if result is not None:
            return result
    return None


def extract(oracle, provenance):
    raw, proof = oracle.read_bytes(), provenance.read_bytes()
    doc, authority = json.loads(raw), json.loads(proof)
    assert len(raw) <= 4 * 1024 * 1024 and len(proof) <= 128 * 1024
    assert doc["schemaVersion"] == 1 and not doc["windowsCreated"] and not doc["filesystemQueries"]
    assert authority["sourcesUnmodified"] and authority["sourceBaseline"] == "ca04f142185c7de40acd8523bdb563195d90a1d1"
    assert len(doc["cases"]) == 10
    paths = None
    for case in doc["cases"]:
        root = case["root"]
        actual = {"refreshArrow": named(root, "storage.refresh.arrow")["shape"]["path"]}
        for key, name in [("settingsFeedback", "storage.settings.button"), ("refreshFeedback", "storage.refresh.button")]:
            actual[key] = named(root, name)["children"][1]["children"][0]["shape"]["path"]
        assert [len(actual[k]) for k in ["refreshArrow", "settingsFeedback", "refreshFeedback"]] == [16, 10, 10]
        assert paths is None or actual == paths, "Control geometry varies across original source cases"
        paths = actual
    relevant = ["Sources/TelemetryCanvases.swift", "Sources/HUDControlHighlightLayer.swift", "Sources/StorageController.swift",
                "windows/tools/storage_presentation_reference.swift", "windows/tools/module_reference_layers.swift"]
    return {"schemaVersion": 1, "sourceBaseline": authority["sourceBaseline"],
            "sourceSHA256": {p: authority["compiledSHA256"][p] for p in relevant},
            "originalOracleSHA256": digest(raw), "originalProvenanceSHA256": digest(proof),
            "derivation": "Exact refresh-arrow filled CGPath and settings/refresh highlight tint paths; identical in all 10 detached dark/light states. No coordinates resampled.",
            "defaultAppearance": {"accent": doc["cases"][0]["accent"], "availableColor": doc["cases"][0]["cyan"]},
            "paths": paths}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("oracle", type=Path)
    parser.add_argument("provenance", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = (json.dumps(extract(args.oracle, args.provenance), ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode()
    assert len(result) <= 16 * 1024
    if args.check:
        assert args.output.read_bytes() == result, "Packaged Storage paths differ from original source"
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as stream:
            stream.write(result)
    print(f"Storage source paths: {len(result)} bytes, SHA256 {digest(result)}")


if __name__ == "__main__":
    main()
